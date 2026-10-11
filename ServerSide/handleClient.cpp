#include <sys/types.h>
#include <sys/socket.h>

#include "../Protocol.h"

#include <netdb.h>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <iostream>

void handleClient(int receiveSocket, int sendSocket)
{
    std::string message;

    while (true)
    {
        ReceiveStatus status =
            receiveLongMessage(receiveSocket, message);

        if (status != ReceiveStatus::OK)
        {
            if (status == ReceiveStatus::Disconnected)
                std::cout << "Client disconnected\n";
            else
                std::cerr << "Receive/protocol error\n";

            break;
        }

        if (message == "/quit")
        {
            // Forward the framed quit message.
            sendLongMessage(sendSocket, message);
            break;
        }

        if (!sendLongMessage(sendSocket, message))
        {
            std::cerr << "Failed to forward message\n";
            break;
        }
    }

    // This is a two-client session: stop the other handler too.
    shutdown(sendSocket, SHUT_RDWR);

    // Each handler closes only its own client socket.
    close(receiveSocket);

    std::cout
        << "Thread finished: "
        << std::this_thread::get_id()
        << std::endl;
}
