#include <string>
#include <poll.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include "../Protocol.h"

class Client
{
private:
    int sockfd;
    std::string username;
    BIO *bio;
    static SSL_CTX *ctx;
    SSL *ssl;

public:
    Client(std::string username);
    int getSockfd();
    SSL_CTX *getCTX();
    SSL_CTX *makeContext();
    bool makeConnection(const char *host, const char *port);
    SSL* createSSLObject(const char* host);
    bool sendMessage(const std::string &message);
    ReceiveStatus receiveMessage(std::string &message);
    void shutdownConnection();
    void closeConnection();
    ~Client();
};
