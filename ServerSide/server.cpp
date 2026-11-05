#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <iostream>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "handleClient.cpp"
#include "sslFunctions.cpp"

#define MYPORT "9000"
#define BACKLOG 10

int main()
{
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    SSL_CTX *ctx = nullptr;
    struct addrinfo hints, *res;
    int sockfd = -1;

    ctx = createServerContext();
    if (ctx == nullptr)
    {
        return 1;
    }

    memset(&hints, 0, sizeof hints);

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int status = getaddrinfo(NULL, MYPORT, &hints, &res);

    if (status != 0)
    {
        std::cerr << "getaddrinfo: " << gai_strerror(status) << std::endl;

        return -1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (sockfd == -1)
    {
        perror("socket");
        freeaddrinfo(res);
        return -1;
    }

    if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1)
    {
        perror("bind");

        close(sockfd);
        freeaddrinfo(res);

        return -1;
    }

    freeaddrinfo(res);

    if (listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        close(sockfd);

        return -1;
    }

    std::cout << "Server listening on port " << MYPORT << std::endl;

    while (true)
    {
        addr_size = sizeof(their_addr);

        int clientA = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);

        if (clientA == -1)
        {
            perror("accept");
            return -1;
        }

        SSL *clientA_ssl = SSL_new(ctx);

        if (clientA_ssl == nullptr)
        {
            ERR_print_errors_fp(stderr);
            close(clientA);
            return -1;
        }

        if (SSL_set_fd(clientA_ssl, clientA) != 1)
        {
            ERR_print_errors_fp(stderr);
            SSL_free(clientA_ssl);
            close(clientA);
            return -1;
        }

        if (SSL_accept(clientA_ssl) <= 0)
        {
            ERR_print_errors_fp(stderr);
            SSL_free(clientA_ssl);
            close(clientA);
            return -1;
        }

        int clientB = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);

        if (clientB == -1)
        {
            perror("accept");
            return -1;
        }

        SSL *clientB_ssl = SSL_new(ctx);

        if (clientB_ssl == nullptr)
        {
            ERR_print_errors_fp(stderr);
            close(clientB);
            return -1;
        }

        if (SSL_set_fd(clientB_ssl, clientB) != 1)
        {
            ERR_print_errors_fp(stderr);
            SSL_free(clientB_ssl);
            close(clientB);
            return -1;
        }

        if (SSL_accept(clientB_ssl) <= 0)
        {
            ERR_print_errors_fp(stderr);
            SSL_free(clientB_ssl);
            close(clientB);
            return -1;
        }

        std::thread(handleClient, clientA_ssl, clientB_ssl, clientA, clientB).detach();
        std::thread(handleClient, clientB_ssl, clientA_ssl, clientA, clientB).detach();
    }

    close(sockfd);

    return 0;
}