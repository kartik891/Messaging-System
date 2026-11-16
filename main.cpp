#include <iostream>
#include <thread>
#include <atomic>
#include <string>
#include <cerrno>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "ClientSide/Client.h"
#include "ClientSide/messageQueue.h"
#include "ClientSide/networkHandler.h"

namespace {
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
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: ./Client <username>\n";
        return 1;
    }

    Client client(argv[1]);

    // The order matters: context -> TCP -> TLS handshake -> nonblocking I/O.
    if (client.makeContext() == nullptr) {
        std::cerr << "Could not initialize TLS context\n";
        return 1;
    }

    if (!client.makeConnection("localhost", "9000")) {
        std::cerr << "TCP connection failed\n";
        return 1;
    }

    if (client.createSSLObject("localhost") == nullptr) {
        std::cerr << "TLS connection failed\n";
        client.closeConnection();
        return 1;
    }

    if (!setNonBlocking(client.getSockfd())) {
        client.closeConnection();
        return 1;
    }

    MessageQueue incoming;
    MessageQueue outgoing;
    std::atomic<bool> running{true};

    NetworkHandler network(
        client.getSSL(),
        client.getSockfd(),
        incoming,
        outgoing,
        running
    );

    // This is the only thread that performs SSL I/O on this SSL object.
    std::thread networkThread(&NetworkHandler::run, &network);

    // Display thread only consumes the incoming queue.
    std::thread displayThread([&]() {
        std::string received;
        while (incoming.waitPop(received)) {
            std::cout << "\nReceived: " << received << '\n'
                      << std::flush;
        }
    });

    std::cout << "Connected as " << argv[1]
              << ". Type /quit to leave.\n";

    // Poll stdin periodically so a remote disconnect can stop the input loop.
    while (running) {
        pollfd inputFd{};
        inputFd.fd = STDIN_FILENO;
        inputFd.events = POLLIN;

        const int pollResult = poll(&inputFd, 1, 200);
        if (pollResult < 0) {
            if (errno == EINTR)
                continue;
            perror("stdin poll");
            break;
        }

        if (pollResult == 0)
            continue;

        if (inputFd.revents & (POLLERR | POLLNVAL))
            break;

        if (inputFd.revents & (POLLIN | POLLHUP)) {
            std::string message;
            if (!std::getline(std::cin, message))
                break;

            if (!outgoing.push(message))
                break;

            if (message == "/quit")
                break;
        }
    }

    // Close the queue to new messages, but NetworkHandler drains anything
    // already queued before it exits.
    outgoing.close();
    running = false;

    networkThread.join();

    incoming.close();
    displayThread.join();

    // No thread is using SSL now. Force-close the transport and release TLS.
    client.shutdownConnection();
    client.closeConnection();

    return 0;
}
