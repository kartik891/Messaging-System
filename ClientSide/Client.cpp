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
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>

SSL_CTX* Client::ctx = nullptr;

Client::Client(std::string username)
{
    sockfd = -1;
    this->username = username;
    bio = nullptr;
    ssl = nullptr;
}

SSL_CTX* Client::makeContext(){
    if(ctx != nullptr){
        return ctx;
    }

    SSL_CTX* currContext = nullptr;
    currContext = SSL_CTX_new(TLS_client_method());

    if(currContext == nullptr){
        std::cerr << "failed to create Context" << std::endl;
        return nullptr;
    }

    SSL_CTX_set_verify(currContext, SSL_VERIFY_PEER, NULL);

    if (SSL_CTX_set_default_verify_paths(currContext) != 1) {
        printf("Failed to set the default trusted certificate store\n");
        SSL_CTX_free(currContext);
        return nullptr;
    }

    if (SSL_CTX_set_min_proto_version(currContext, TLS1_2_VERSION) != 1) {
        printf("Failed to set the minimum TLS protocol version\n");
        SSL_CTX_free(currContext);
        return nullptr;
    }

    ctx = currContext;
    return ctx;
}

SSL_CTX* Client::getCTX(){
    return ctx;
}

int Client::getSockfd(){
    return sockfd;
}

bool Client::makeConnection(const char *host, const char *port)
{
    if(sockfd != -1){
        std::cout << "Connetion already established\n"; 
        return true;
    }

    BIO_ADDRINFO *res = nullptr;

    int status = BIO_lookup_ex(host, port, BIO_LOOKUP_CLIENT, AF_UNSPEC, SOCK_STREAM, 0, &res);

    if (status != 1)
    {
        std::cerr << "BIO_Lookup_ex Error";
        ERR_print_errors_fp(stderr);
        return false;
    }

    for(const BIO_ADDRINFO* ai = res; ai != nullptr; ai = BIO_ADDRINFO_next(ai)){
        int currSock = BIO_socket(BIO_ADDRINFO_family(ai), SOCK_STREAM, 0, 0);

        if(currSock == -1){
            continue;
        }

        if(BIO_connect(currSock, BIO_ADDRINFO_address(ai), 0) != 1){
            BIO_closesocket(currSock);
            continue;
        }

        sockfd = currSock;
        break;
    }

    // make a socket, bind it, and listen on it:

    BIO_ADDRINFO_free(res);

    if (sockfd == -1)
    {
        std::cerr << "Failed to connect" << std::endl;
        return false;
    }

    return true;
}


SSL* Client::createSSLObject(const char* host)
{
    if (ctx == nullptr)
    {
        std::cerr << "SSL context is not initialized\n";
        return nullptr;
    }

    if (sockfd == -1)
    {
        std::cerr << "TCP connection is not established\n";
        return nullptr;
    }

    if(ssl != nullptr){
        std::cerr << "SSL Object already created\n";
        return nullptr; 
    }

    SSL* currSSL = SSL_new(ctx);

    if (currSSL == nullptr)
    {
        std::cerr << "Failed to create SSL object\n";
        ERR_print_errors_fp(stderr);
        return nullptr;
    }

    if (SSL_set_fd(currSSL, sockfd) != 1)
    {
        std::cerr << "Failed to attach socket to SSL object\n";
        ERR_print_errors_fp(stderr);
        SSL_free(currSSL);
        return nullptr;
    }

    // Set SNI: tells the server which hostname we want.
    if (SSL_set_tlsext_host_name(currSSL, host) != 1)
    {
        std::cerr << "Failed to set SNI hostname\n";
        ERR_print_errors_fp(stderr);
        SSL_free(currSSL);
        return nullptr;
    }

    // Verify that the server certificate matches the hostname.
    if (SSL_set1_host(currSSL, host) != 1)
    {
        std::cerr << "Failed to configure hostname verification\n";
        ERR_print_errors_fp(stderr);
        SSL_free(currSSL);
        return nullptr;
    }

    // Perform the TLS handshake.
    int result = SSL_connect(currSSL);

    if (result != 1)
    {
        int error = SSL_get_error(currSSL, result);

        std::cerr << "TLS handshake failed. SSL error: "
                  << error << '\n';

        ERR_print_errors_fp(stderr);

        long verifyResult = SSL_get_verify_result(currSSL);

        if (verifyResult != X509_V_OK)
        {
            std::cerr << "Certificate verification failed: "
                      << X509_verify_cert_error_string(verifyResult)
                      << '\n';
        }

        SSL_free(currSSL);
        return nullptr;
    }

    std::cout << "TLS handshake successful\n"
              << "Protocol: " << SSL_get_version(currSSL) << '\n'
              << "Cipher: "
              << SSL_get_cipher(currSSL) << '\n';

    ssl = currSSL;
    return ssl;
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
