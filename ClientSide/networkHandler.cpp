#include "networkHandler.h"

#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <utility>

#include <openssl/err.h>

NetworkHandler::NetworkHandler(
    SSL* ssl,
    int sockfd,
    MessageQueue& incoming,
    MessageQueue& outgoing,
    std::atomic<bool>& running
)
    : ssl(ssl),
      sockfd(sockfd),
      incoming(incoming),
      outgoing(outgoing),
      running(running) {}

bool NetworkHandler::prepareOutgoingMessage() {
    if (!sendBuffer.empty())
        return true;

    std::string message;
    if (!outgoing.tryPop(message))
        return false;

    if (message.size() > MAX_MESSAGE_SIZE) {
        std::cerr << "Outgoing message exceeds 1 MiB and was discarded\n";
        return false;
    }

    const uint32_t networkLength = htonl(static_cast<uint32_t>(message.size()));
    sendBuffer.clear();
    sendBuffer.reserve(sizeof(networkLength) + message.size());
    sendBuffer.append(
        reinterpret_cast<const char*>(&networkLength),
        sizeof(networkLength)
    );
    sendBuffer.append(message);
    sendOffset = 0;
    return true;
}

IOResult NetworkHandler::processWrite() {
    if (sendBuffer.empty())
        return IOResult::Progress;

    size_t bytesWritten = 0;
    const int result = SSL_write_ex(
        ssl,
        sendBuffer.data() + sendOffset,
        sendBuffer.size() - sendOffset,
        &bytesWritten
    );

    if (result == 1) {
        if (bytesWritten == 0)
            return IOResult::Error;

        sendOffset += bytesWritten;
        if (sendOffset == sendBuffer.size()) {
            sendBuffer.clear();
            sendOffset = 0;
        }
        return IOResult::Progress;
    }

    // SSL_get_error must immediately follow the failed SSL I/O call.
    const int error = SSL_get_error(ssl, result);
    switch (error) {
        case SSL_ERROR_WANT_READ:
            return IOResult::WantRead;
        case SSL_ERROR_WANT_WRITE:
            return IOResult::WantWrite;
        case SSL_ERROR_ZERO_RETURN:
            return IOResult::Closed;
        default:
            ERR_print_errors_fp(stderr);
            return IOResult::Error;
    }
}

IOResult NetworkHandler::processRead() {
    char* target = nullptr;
    size_t remaining = 0;

    if (!readingPayload) {
        target = header + headerOffset;
        remaining = sizeof(header) - headerOffset;
    } else {
        target = receiveBuffer.data() + receiveOffset;
        remaining = static_cast<size_t>(messageLength) - receiveOffset;
    }

    if (remaining == 0)
        return IOResult::Progress;

    size_t bytesRead = 0;
    const int result = SSL_read_ex(ssl, target, remaining, &bytesRead);

    if (result != 1) {
        const int error = SSL_get_error(ssl, result);
        switch (error) {
            case SSL_ERROR_WANT_READ:
                return IOResult::WantRead;
            case SSL_ERROR_WANT_WRITE:
                return IOResult::WantWrite;
            case SSL_ERROR_ZERO_RETURN:
                return IOResult::Closed;
            default:
                ERR_print_errors_fp(stderr);
                return IOResult::Error;
        }
    }

    if (bytesRead == 0)
        return IOResult::Error;

    if (!readingPayload) {
        headerOffset += bytesRead;

        if (headerOffset == sizeof(header)) {
            uint32_t networkLength = 0;
            std::memcpy(&networkLength, header, sizeof(networkLength));
            messageLength = ntohl(networkLength);

            if (messageLength > MAX_MESSAGE_SIZE) {
                std::cerr << "Incoming message exceeds 1 MiB\n";
                return IOResult::Error;
            }

            receiveBuffer.clear();
            receiveOffset = 0;

            if (messageLength == 0) {
                if (!incoming.push(std::string{}))
                    return IOResult::Closed;
                resetReceiveState();
            } else {
                receiveBuffer.resize(messageLength);
                readingPayload = true;
            }
        }
    } else {
        receiveOffset += bytesRead;

        if (receiveOffset == messageLength) {
            if (!incoming.push(std::move(receiveBuffer)))
                return IOResult::Closed;
            resetReceiveState();
        }
    }

    return IOResult::Progress;
}

void NetworkHandler::resetReceiveState() {
    std::memset(header, 0, sizeof(header));
    headerOffset = 0;
    messageLength = 0;
    receiveBuffer.clear();
    receiveOffset = 0;
    readingPayload = false;
}

void NetworkHandler::run() {
    if (ssl == nullptr || sockfd < 0) {
        running = false;
        incoming.close();
        outgoing.close();
        return;
    }

    bool connectionAlive = true;
    bool readWantWrite = false;
    bool writeWantRead = false;
    bool writeWantWrite = false;

    while (connectionAlive) {
        prepareOutgoingMessage();
        const bool hasPendingWrite = !sendBuffer.empty();

        // When SSL_write_ex returned WANT_WRITE, retry that exact write
        // before initiating any other TLS I/O operation.
        const bool retryWriteFirst = hasPendingWrite && writeWantWrite;

        // Once the application asks to stop, finish messages already queued.
        if (!running && outgoing.empty() && !hasPendingWrite)
            break;

        // SSL may already hold decrypted application bytes even when the
        // kernel socket is no longer readable. Do not sleep in poll() in
        // that case; consume the buffered TLS plaintext first.
        const bool bufferedBeforePoll = !retryWriteFirst &&
            !readWantWrite && SSL_pending(ssl) > 0;

        pollfd pfd{};
        pfd.fd = sockfd;

        if (retryWriteFirst) {
            pfd.events = POLLOUT;
        } else {
            pfd.events = readWantWrite ? POLLOUT : POLLIN;
            if (hasPendingWrite)
                pfd.events |= writeWantRead ? POLLIN : POLLOUT;
        }

        const int pollResult = bufferedBeforePoll ? 0 : poll(&pfd, 1, 50);
        if (pollResult < 0) {
            if (errno == EINTR)
                continue;
            perror("NetworkHandler poll");
            break;
        }

        if (pollResult == 0 && !bufferedBeforePoll)
            continue;

        if (pfd.revents & POLLNVAL) {
            std::cerr << "NetworkHandler: invalid socket descriptor\n";
            break;
        }

        if (pfd.revents & POLLERR) {
            int socketError = 0;
            socklen_t errorLength = sizeof(socketError);
            if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR,
                           &socketError, &errorLength) == 0) {
                std::cerr << "NetworkHandler socket error: "
                          << socketError << '\n';
            }
            break;
        }

        const bool readable = (pfd.revents & POLLIN) != 0;
        const bool writable = (pfd.revents & POLLOUT) != 0;
        const bool hungUp = (pfd.revents & POLLHUP) != 0;

        if (hasPendingWrite) {
            const bool canRetryWrite = writeWantRead ? readable : writable;
            if (canRetryWrite) {
                const IOResult status = processWrite();
                switch (status) {
                    case IOResult::Progress:
                        writeWantRead = false;
                        writeWantWrite = false;
                        break;
                    case IOResult::WantRead:
                        writeWantRead = true;
                        writeWantWrite = false;
                        break;
                    case IOResult::WantWrite:
                        writeWantRead = false;
                        writeWantWrite = true;
                        break;
                    case IOResult::Closed:
                        connectionAlive = false;
                        break;
                    case IOResult::Error:
                        std::cerr << "TLS write failed\n";
                        connectionAlive = false;
                        break;
                }
            }
        }

        if (!connectionAlive)
            break;

        // OpenSSL permits SSL_read_ex after SSL_write_ex returned WANT_READ.
        // If the write now needs POLLOUT, retry it before any other TLS I/O.
        const bool writeBlockedOnWrite =
            !sendBuffer.empty() && writeWantWrite;
        const bool canRetryRead = readWantWrite
            ? writable
            : (readable || hungUp);
        const bool hasBufferedPlaintext = bufferedBeforePoll ||
            (!readWantWrite && SSL_pending(ssl) > 0);

        if (!writeBlockedOnWrite && (canRetryRead || hasBufferedPlaintext)) {
            const IOResult status = processRead();
            switch (status) {
                case IOResult::Progress:
                case IOResult::WantRead:
                    readWantWrite = false;
                    break;
                case IOResult::WantWrite:
                    readWantWrite = true;
                    break;
                case IOResult::Closed:
                    connectionAlive = false;
                    break;
                case IOResult::Error:
                    std::cerr << "TLS read failed\n";
                    connectionAlive = false;
                    break;
            }
        }

        if (hungUp && !readable && SSL_pending(ssl) == 0)
            connectionAlive = false;
    }

    running = false;
    incoming.close();
    outgoing.close();
}
