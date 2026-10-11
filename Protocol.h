#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <cstdint>
#include <string>
#include <limits>
#include <cerrno>
#include <cstdio>

enum class ReceiveStatus
{
    OK,
    Disconnected,
    Error,
    NoData
};

const uint32_t MAXSIZE = 1024 * 1024;

inline bool sendWholeMessage(int fd, const void *msg, size_t length)
{

    const char *ptr = static_cast<const char *>(msg);
    size_t sent = 0;

    while (sent < length)
    {

        ssize_t n = send(fd, ptr + sent, length - sent, MSG_NOSIGNAL);

        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            perror("send");
            return false;
        }

        if (n == 0)
        {
            return false;
        }

        sent += static_cast<size_t>(n);
    }

    return true;
}

inline ReceiveStatus receiveWholeMessage(int fd, void *msg, size_t length)
{
    char *ptr = static_cast<char *>(msg);
    size_t received = 0;

    while (received < length)
    {
        ssize_t n = recv(fd, ptr + received, length - received, 0);

        if (n < 0)
        {
            perror("recieve");
            return ReceiveStatus::Error;
        }

        if (n == 0)
        {
            return ReceiveStatus::Disconnected;
        }

        received += static_cast<ssize_t>(n);
    }

    return ReceiveStatus::OK;
}

inline bool sendLongMessage(int fd, const std::string &message)
{
    if (message.size() > MAXSIZE)
        return false;

    uint32_t length = htonl(
        static_cast<uint32_t>(message.size()));

    if (!sendWholeMessage(fd, &length, sizeof(length)))
        return false;

    if (!message.empty() &&
        !sendWholeMessage(fd, message.data(), message.size()))
        return false;

    return true;
}

inline ReceiveStatus receiveLongMessage(
    int fd, std::string &message)
{
    uint32_t networkLength = 0;

    ReceiveStatus status = receiveWholeMessage(
        fd, &networkLength, sizeof(networkLength));

    if (status != ReceiveStatus::OK)
        return status;

    uint32_t length = ntohl(networkLength);

    if (length > MAXSIZE)
        return ReceiveStatus::Error;

    message.resize(length);

    if (length == 0)
        return ReceiveStatus::OK;

    status = receiveWholeMessage(fd, message.data(), length);

    if (status != ReceiveStatus::OK)
        message.clear();

    return status;
}


#endif