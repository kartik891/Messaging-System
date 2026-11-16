#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <utility>

#include "../ClientSide/messageQueue.h"
#include "../ClientSide/networkHandler.h"
#include "sslFunctions.h"

namespace {
constexpr const char* SERVER_PORT = "9000";
constexpr int LISTEN_BACKLOG = 10;

bool setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL)");
        return false;
    }

    return true;
}

int createListeningSocket() {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* results = nullptr;
    const int lookupStatus = getaddrinfo(
        nullptr, SERVER_PORT, &hints, &results
    );

    if (lookupStatus != 0) {
        std::cerr << "getaddrinfo: " << gai_strerror(lookupStatus) << '\n';
        return -1;
    }

    int listenFd = -1;
    for (addrinfo* address = results;
         address != nullptr;
         address = address->ai_next) {

        const int candidate = socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol
        );

        if (candidate == -1)
            continue;

        int reuse = 1;
        setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        if (bind(candidate, address->ai_addr, address->ai_addrlen) == 0 &&
            listen(candidate, LISTEN_BACKLOG) == 0) {
            listenFd = candidate;
            break;
        }

        close(candidate);
    }

    freeaddrinfo(results);
    return listenFd;
}

int acceptClient(int listenFd) {
    while (true) {
        const int clientFd = accept(listenFd, nullptr, nullptr);
        if (clientFd >= 0)
            return clientFd;
        if (errno == EINTR)
            continue;
        perror("accept");
        return -1;
    }
}

SSL* acceptTLSClient(SSL_CTX* ctx, int clientFd) {
    SSL* ssl = SSL_new(ctx);
    if (ssl == nullptr) {
        ERR_print_errors_fp(stderr);
        return nullptr;
    }

    if (SSL_set_fd(ssl, clientFd) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return nullptr;
    }

    const int result = SSL_accept(ssl);
    if (result != 1) {
        const int sslError = SSL_get_error(ssl, result);
        std::cerr << "TLS handshake with client failed: "
                  << sslError << '\n';
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return nullptr;
    }

    std::cout << "Accepted TLS client using "
              << SSL_get_version(ssl) << " / "
              << SSL_get_cipher(ssl) << '\n';
    return ssl;
}

// One NetworkHandler owns each SSL object. A relay thread moves completed
// messages between queues. No SSL object is shared by multiple threads.
void relaySession(
    MessageQueue& fromA,
    MessageQueue& fromB,
    MessageQueue& toA,
    MessageQueue& toB,
    std::atomic<bool>& runningA,
    std::atomic<bool>& runningB
) {
    while (runningA || runningB || !fromA.empty() || !fromB.empty()) {
        bool didWork = false;
        std::string message;

        while (fromA.tryPop(message)) {
            toB.push(std::move(message));
            didWork = true;
        }

        while (fromB.tryPop(message)) {
            toA.push(std::move(message));
            didWork = true;
        }

        // If either client connection ends, stop the other side after
        // transferring messages already received from the departing side.
        if (!runningA && fromA.empty())
            runningB = false;

        if (!runningB && fromB.empty())
            runningA = false;

        if (!didWork)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    toA.close();
    toB.close();
}

void runPairedSession(
    SSL* sslA, int fdA,
    SSL* sslB, int fdB
) {
    if (!setNonBlocking(fdA) || !setNonBlocking(fdB)) {
        SSL_free(sslA);
        SSL_free(sslB);
        close(fdA);
        close(fdB);
        return;
    }

    MessageQueue fromA;
    MessageQueue fromB;
    MessageQueue toA;
    MessageQueue toB;

    std::atomic<bool> runningA{true};
    std::atomic<bool> runningB{true};

    // A handler reads from its own client and writes only to that client.
    NetworkHandler handlerA(sslA, fdA, fromA, toA, runningA);
    NetworkHandler handlerB(sslB, fdB, fromB, toB, runningB);

    std::thread threadA(&NetworkHandler::run, &handlerA);
    std::thread threadB(&NetworkHandler::run, &handlerB);

    std::thread relayThread(
        relaySession,
        std::ref(fromA), std::ref(fromB),
        std::ref(toA), std::ref(toB),
        std::ref(runningA), std::ref(runningB)
    );

    threadA.join();
    threadB.join();
    relayThread.join();

    // Both NetworkHandler threads have stopped using these SSL objects.
    SSL_shutdown(sslA);
    SSL_shutdown(sslB);
    SSL_free(sslA);
    SSL_free(sslB);
    close(fdA);
    close(fdB);

    std::cout << "Two-client session ended\n";
}
}

int main() {
    SSL_CTX* ctx = createServerContext();
    if (ctx == nullptr)
        return 1;

    const int listenFd = createListeningSocket();
    if (listenFd == -1) {
        std::cerr << "Could not create listening socket\n";
        SSL_CTX_free(ctx);
        return 1;
    }

    std::cout << "Server listening on port " << SERVER_PORT << '\n';

    while (true) {
        std::cout << "Waiting for client A...\n";
        const int fdA = acceptClient(listenFd);
        if (fdA == -1)
            continue;

        SSL* sslA = acceptTLSClient(ctx, fdA);
        if (sslA == nullptr) {
            close(fdA);
            continue;
        }

        std::cout << "Waiting for client B...\n";
        const int fdB = acceptClient(listenFd);
        if (fdB == -1) {
            SSL_shutdown(sslA);
            SSL_free(sslA);
            close(fdA);
            continue;
        }

        SSL* sslB = acceptTLSClient(ctx, fdB);
        if (sslB == nullptr) {
            close(fdB);
            SSL_shutdown(sslA);
            SSL_free(sslA);
            close(fdA);
            continue;
        }

        // Session owns both SSL objects and their sockets after this call.
        std::thread(
            runPairedSession,
            sslA, fdA,
            sslB, fdB
        ).detach();
    }

    close(listenFd);
    SSL_CTX_free(ctx);
    return 0;
}
