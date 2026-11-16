#include "Client.h"

#include <openssl/bio.h>
#include <openssl/err.h>

#include <sys/socket.h>
#include <unistd.h>

#include <iostream>

Client::Client(std::string username)
    : username(std::move(username)) {}

SSL_CTX* Client::makeContext() {
    if (ctx != nullptr)
        return ctx;

    SSL_CTX* newContext = SSL_CTX_new(TLS_client_method());
    if (newContext == nullptr) {
        std::cerr << "Failed to create TLS client context\n";
        ERR_print_errors_fp(stderr);
        return nullptr;
    }

    SSL_CTX_set_verify(newContext, SSL_VERIFY_PEER, nullptr);

    // Run the client from the project root directory.
    if (SSL_CTX_load_verify_locations(
            newContext, "certs/ca.cert", nullptr) != 1) {
        std::cerr << "Failed to load certs/ca.cert\n";
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(newContext);
        return nullptr;
    }

    if (SSL_CTX_set_min_proto_version(newContext, TLS1_2_VERSION) != 1) {
        std::cerr << "Failed to set minimum TLS version\n";
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(newContext);
        return nullptr;
    }

    ctx = newContext;
    return ctx;
}

SSL_CTX* Client::getCTX() const {
    return ctx;
}

int Client::getSockfd() const {
    return sockfd;
}

SSL* Client::getSSL() const {
    return ssl;
}

bool Client::makeConnection(const char* host, const char* port) {
    if (sockfd != -1) {
        std::cerr << "TCP connection already established\n";
        return true;
    }

    BIO_ADDRINFO* resultList = nullptr;
    const int lookupResult = BIO_lookup_ex(
        host, port, BIO_LOOKUP_CLIENT, AF_UNSPEC, SOCK_STREAM, 0, &resultList
    );

    if (lookupResult != 1) {
        std::cerr << "BIO_lookup_ex failed\n";
        ERR_print_errors_fp(stderr);
        return false;
    }

    for (const BIO_ADDRINFO* address = resultList;
         address != nullptr;
         address = BIO_ADDRINFO_next(address)) {

        const int candidate = BIO_socket(
            BIO_ADDRINFO_family(address), SOCK_STREAM, 0, 0
        );
        if (candidate == -1)
            continue;

        if (BIO_connect(candidate, BIO_ADDRINFO_address(address), 0) != 1) {
            BIO_closesocket(candidate);
            continue;
        }

        sockfd = candidate;
        break;
    }

    BIO_ADDRINFO_free(resultList);

    if (sockfd == -1) {
        std::cerr << "Failed to establish TCP connection to "
                  << host << ':' << port << '\n';
        return false;
    }

    return true;
}

SSL* Client::createSSLObject(const char* host) {
    if (ctx == nullptr || sockfd == -1) {
        std::cerr << "Create TLS context and TCP connection before TLS setup\n";
        return nullptr;
    }

    if (ssl != nullptr) {
        std::cerr << "TLS connection already initialized\n";
        return nullptr;
    }

    SSL* newSSL = SSL_new(ctx);
    if (newSSL == nullptr) {
        ERR_print_errors_fp(stderr);
        return nullptr;
    }

    if (SSL_set_fd(newSSL, sockfd) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_free(newSSL);
        return nullptr;
    }

    if (SSL_set_tlsext_host_name(newSSL, host) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_free(newSSL);
        return nullptr;
    }

    if (SSL_set1_host(newSSL, host) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_free(newSSL);
        return nullptr;
    }

    // The socket is blocking during this handshake. It becomes nonblocking
    // after createSSLObject() returns and before NetworkHandler starts.
    const int result = SSL_connect(newSSL);
    if (result != 1) {
        const int sslError = SSL_get_error(newSSL, result);
        std::cerr << "TLS handshake failed. SSL error: " << sslError << '\n';
        ERR_print_errors_fp(stderr);

        const long verifyResult = SSL_get_verify_result(newSSL);
        if (verifyResult != X509_V_OK) {
            std::cerr << "Certificate verification failed: "
                      << X509_verify_cert_error_string(verifyResult) << '\n';
        }

        SSL_free(newSSL);
        return nullptr;
    }

    ssl = newSSL;

    std::cout << "TLS handshake successful\n"
              << "Protocol: " << SSL_get_version(ssl) << '\n'
              << "Cipher: " << SSL_get_cipher(ssl) << '\n';

    return ssl;
}

void Client::shutdownConnection() {
    // Called only after NetworkHandler has joined, so this thread now owns SSL.
    // A return value of 0 means our close_notify was sent and the peer reply
    // has not yet been read. That is sufficient for this short-lived client.
    if (ssl != nullptr) {
        const int result = SSL_shutdown(ssl);
        if (result < 0) {
            const int error = SSL_get_error(ssl, result);
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
                std::cerr << "TLS shutdown reported an error\n";
                ERR_print_errors_fp(stderr);
            }
        }
    }
}

void Client::closeConnection() {
    if (ssl != nullptr) {
        SSL_free(ssl);
        ssl = nullptr;
    }

    if (sockfd != -1) {
        ::close(sockfd);
        sockfd = -1;
    }
}

Client::~Client() {
    closeConnection();

    if (ctx != nullptr) {
        SSL_CTX_free(ctx);
        ctx = nullptr;
    }
}
