#include "sslFunctions.h"

#include <openssl/err.h>

#include <iostream>

SSL_CTX* createServerContext() {
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (ctx == nullptr) {
        ERR_print_errors_fp(stderr);
        return nullptr;
    }

    if (SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return nullptr;
    }

    SSL_CTX_set_options(ctx, SSL_OP_NO_RENEGOTIATION);

    if (SSL_CTX_use_certificate_file(
            ctx, "certs/server.crt", SSL_FILETYPE_PEM) != 1) {
        std::cerr << "Failed to load certs/server.crt\n";
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return nullptr;
    }

    if (SSL_CTX_use_PrivateKey_file(
            ctx, "certs/server.key", SSL_FILETYPE_PEM) != 1) {
        std::cerr << "Failed to load certs/server.key\n";
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return nullptr;
    }

    if (SSL_CTX_check_private_key(ctx) != 1) {
        std::cerr << "Server private key does not match certificate\n";
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return nullptr;
    }

    // This is a local two-client demo. The server authenticates itself to
    // clients; it does not require client certificates.
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

    return ctx;
}
