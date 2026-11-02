
#include <iostream>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <openssl/bio.h>
#include "ClientSide/Client.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./Client <username>\n";
        return 1;
    }

    Client cli(argv[1]);

    BIO* ConnObj = cli.makeConnection("localhost", "9000");

    if(ConnObj == nullptr){
        return 1;
    }

    std::cout << "BIO OBJ: " << ConnObj << std::endl;

    pollfd fds[2]{};

    // Monitor keyboard input.
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    // Monitor messages from the server.
    fds[1].fd = cli.getSockfd();
    fds[1].events = POLLIN;

    std::string msg;
    bool running = true;

    while (running) {
        int result = poll(fds, 2, -1);

        if (result < 0) {
            if (errno == EINTR)
                continue;

            perror("poll");
            break;
        }

        // Check the socket first.
        if (fds[1].revents & POLLNVAL) {
            std::cerr << "Invalid socket\n";
            break;
        }

        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            std::string incoming;

            ReceiveStatus status = cli.receiveMessage(incoming);

            if (status == ReceiveStatus::Disconnected) {
                std::cout << "\nServer disconnected\n";
                break;
            }

            if (status == ReceiveStatus::Error) {
                std::cerr << "\nError receiving message\n";
                break;
            }

            if (status == ReceiveStatus::OK) {
                if (incoming == "/quit" || incoming == "") {
                    std::cout << "\nOther client disconnected\n";
                    break;
                }
                
                std::cout << "\nReceived Message: " << incoming << "\n"
                          << std::flush;
            }
        }

        // Check keyboard input.
        if (fds[0].revents & POLLIN) {
            if (!std::getline(std::cin, msg))
                break;
             
            if (!cli.sendMessage(msg)) {
                std::cerr << "Failed to send message\n";
                break;
            }

            if (msg == "/quit" || msg == "")
                break;

        }
    }

    cli.shutdownConnection();
    cli.closeConnection();

    return 0;
}
