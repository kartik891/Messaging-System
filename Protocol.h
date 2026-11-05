#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <sys/socket.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <cstdint>
#include <string>
#include <limits>
#include <cerrno>
#include <cstdio>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <iostream>

enum class ReceiveStatus
{
    OK,
    Disconnected,
    Error,
    NoData
};

const uint32_t MAXSIZE = 1024 * 1024;

inline bool sendWholeMessage(SSL* ssl, const void *msg, size_t length)
{

    const char *ptr = static_cast<const char *>(msg);
    size_t sent = 0;

    while (sent < length)
    {

        size_t bytesWritten = 0; 
        int result = SSL_write_ex(ssl, ptr + sent, length - sent, &bytesWritten);

        if (result != 1)
        {
            int error = SSL_get_error(ssl, result);
            if(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE){
                continue;
            }

            std::cerr << "Error while sending\n";
            return false;
        }

        sent += bytesWritten;
        continue;
    }

    return true;
}

inline ReceiveStatus receiveWholeMessage(SSL* ssl, void *msg, size_t length)
{
    char *ptr = static_cast<char *>(msg);
    size_t received = 0;

    while (received < length)
    {
        size_t readBytes = 0;
        int result = SSL_read_ex(ssl, ptr + received, length - received, &readBytes);

        if (result == 1) {
            received += readBytes;
            continue;
        }

        int error = SSL_get_error(ssl, result);

        if (error == SSL_ERROR_WANT_READ ||
            error == SSL_ERROR_WANT_WRITE) {
            continue;
        }

        if (error == SSL_ERROR_ZERO_RETURN) {
            std::cerr << "TLS connection closed gracefully\n";
            return ReceiveStatus::Disconnected;
        } else {
            ERR_print_errors_fp(stderr);
        }

        return ReceiveStatus::Error;
    }    

    return ReceiveStatus::OK;
}

inline bool sendMessage(SSL* ssl, const std::string &message)
{
    if (ssl ==nullptr)
    {
        return false;
    }

    uint32_t messageLength = htonl(message.size());

    if(!sendWholeMessage(ssl, &messageLength, sizeof(messageLength))){
        return false;
    }

    if(!sendWholeMessage(ssl, message.data(), message.size())){
        return false;
    }

    return true;
}

inline ReceiveStatus receiveMessage(SSL* ssl, std::string& message)
{
    if (ssl == nullptr)
        return ReceiveStatus::Error;

    uint32_t networkLength;
    ReceiveStatus status = receiveWholeMessage(ssl, &networkLength, sizeof(networkLength));

    if (status != ReceiveStatus::OK)
        return status;

    uint32_t messageLength = ntohl(networkLength);

    if (messageLength > MAXSIZE) {
        std::cerr << "Message exceeds maximum allowed size\n";
        return ReceiveStatus::Error;
    }

    message.resize(messageLength);

    if (messageLength == 0)
        return ReceiveStatus::OK;

    return receiveWholeMessage(ssl, message.data(), messageLength);
}

#endif