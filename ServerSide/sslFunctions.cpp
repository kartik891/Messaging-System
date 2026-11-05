#include <openssl/err.h>
#include <openssl/ssl.h>
#include <iostream>
#include <unistd.h>
#include <err.h>
#include <string>

SSL_CTX* createServerContext()
{
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

    if (SSL_CTX_use_certificate_file(ctx, "../certs/server.crt", SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return nullptr;
    }

    if (SSL_CTX_use_PrivateKey_file(
            ctx, "../certs/server.key", SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return nullptr;
    }

    if (SSL_CTX_check_private_key(ctx) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return nullptr;
    }

    static const unsigned char cache_id[] = "MessagingServer";

    if (SSL_CTX_set_session_id_context(
            ctx, cache_id, sizeof(cache_id) - 1) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return nullptr;
    }

    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);
    SSL_CTX_sess_set_cache_size(ctx, 1024);

    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

    return ctx;
}