#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include <openssl/ssl.h>

class Client {
private:
    int sockfd = -1;
    std::string username;
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;

public:
    explicit Client(std::string username);

    SSL_CTX* makeContext();
    SSL_CTX* getCTX() const;
    int getSockfd() const;
    SSL* getSSL() const;

    bool makeConnection(const char* host, const char* port);
    SSL* createSSLObject(const char* host);

    void shutdownConnection();
    void closeConnection();

    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
};

#endif
