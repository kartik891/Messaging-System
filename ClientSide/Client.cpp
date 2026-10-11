#include "Client.h"
#include "../Protocol.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <cstring>
#include <unistd.h>
#include <utility>
#include <iostream>
#include <poll.h>
#include <string>

Client::Client(std::string username)
{
    sockfd = -1;
    this->username = username;
}

int Client::getSockfd()
{
    return sockfd;
}

bool Client::makeConnection(const char *host, const char *port)
{

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     // use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_STREAM; // TCP connection, use SOCK_DGRAM for UDP

    int status = getaddrinfo(host, port, &hints, &res);

    if (status != 0)
    {
        std::cerr << "getaddrinfo: " << gai_strerror(status) << std::endl;
        return false;
    }

    // make a socket, bind it, and listen on it:

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (sockfd == -1)
    {
        perror("Socket");
        freeaddrinfo(res);
        return false;
    }

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) == -1)
    {
        perror("Not Connected");
        close(sockfd);
        sockfd = -1;
        freeaddrinfo(res);
        return false;
    }

    freeaddrinfo(res);

    return true;
}

bool Client::sendMessage(const std::string &message)
{
    if (sockfd == -1)
    {
        return false;
    }

    return sendLongMessage(sockfd, message);
}

ReceiveStatus Client::receiveMessage(std::string& message)
{
    if(sockfd == -1){
        return ReceiveStatus::Disconnected;
    }

    return receiveLongMessage(sockfd, message);
}

void Client::shutdownConnection()
{
    if (sockfd != -1)
    {
        shutdown(sockfd, SHUT_RDWR);
    }
}

void Client::closeConnection()
{
    if (sockfd != -1)
    {
        close(sockfd);
        sockfd = -1;
    }
}

Client::~Client()
{
    closeConnection();
}
