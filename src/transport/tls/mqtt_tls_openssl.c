/**
 * @file mqtt_tls_openssl.c
 * @brief TLS Transport Implementation using OpenSSL
 *
 * This file implements the TLS transport layer for MQTT connections using
 * OpenSSL as the TLS backend. It provides secure encrypted connections with
 * support for:
 * - Certificate verification (CA, hostname)
 * - Client certificates (mTLS)
 * - Server Name Indication (SNI)
 * - ALPN protocol negotiation
 * - Non-blocking handshake
 */

#include "mqtt_tls.h"
#include "mqtt/mqtt_config.h"

#ifdef MQTT_ENABLE_TLS

#include <stdlib.h>
#include <string.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <errno.h>

/*******************************************************************************
 * OpenSSL-specific TLS Context Structure
 ******************************************************************************/

/**
 * @brief OpenSSL-specific TLS context
 */
struct mqtt_tls_context {
    SSL_CTX *ssl_ctx;       /**< OpenSSL SSL context */
    SSL     *ssl;           /**< OpenSSL SSL session */
    int      want_read;     /**< SSL_ERROR_WANT_READ flag */
    int      want_write;    /**< SSL_ERROR_WANT_WRITE flag */
};

/*******************************************************************************
 * Forward Declarations - TLS Transport Operations
 ******************************************************************************/

static mqtt_error_t tls_connect(mqtt_transport_t *transport,
                                const char *host, uint16_t port,
                                uint32_t timeout_ms);
static mqtt_error_t tls_disconnect(mqtt_transport_t *transport);
static ssize_t      tls_send(mqtt_transport_t *transport,
                             const void *buf, size_t len);
static ssize_t      tls_recv(mqtt_transport_t *transport,
                             void *buf, size_t len);
static int          tls_get_fd(mqtt_transport_t *transport);
static mqtt_error_t tls_set_blocking(mqtt_transport_t *transport, bool blocking);
static void         tls_destroy(mqtt_transport_t *transport);

/*******************************************************************************
 * TLS Transport Operations Vtable
 ******************************************************************************/

static const mqtt_transport_ops_t tls_ops = {
    .connect      = tls_connect,
    .disconnect   = tls_disconnect,
    .send         = tls_send,
    .recv         = tls_recv,
    .get_fd       = tls_get_fd,
    .set_blocking = tls_set_blocking,
    .destroy      = tls_destroy
};

const mqtt_transport_ops_t *mqtt_tls_get_ops(void)
{
    return &tls_ops;
}

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

/**
 * @brief Convert OpenSSL error to MQTT error code
 */
static mqtt_error_t convert_ssl_error(SSL *ssl, int result)
{
    int ssl_error = SSL_get_error(ssl, result);

    switch (ssl_error) {
        case SSL_ERROR_NONE:
            return MQTT_OK;
        case SSL_ERROR_WANT_READ:
            return MQTT_ERR_WOULD_BLOCK;
        case SSL_ERROR_WANT_WRITE:
            return MQTT_ERR_WOULD_BLOCK;
        case SSL_ERROR_ZERO_RETURN:
            return MQTT_ERR_CONNECTION_LOST;
        case SSL_ERROR_SYSCALL:
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return MQTT_ERR_WOULD_BLOCK;
            }
            return MQTT_ERR_SOCKET;
        case SSL_ERROR_SSL:
            return MQTT_ERR_TLS_HANDSHAKE;
        default:
            return MQTT_ERR_INTERNAL;
    }
}

/**
 * @brief Create and configure SSL context
 */
static SSL_CTX *create_ssl_context(const mqtt_tls_config_t *config)
{
    /* Create SSL context with TLS client method */
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == NULL) {
        return NULL;
    }

    /* Set minimum TLS version (default to TLS 1.2) */
    int min_version = TLS1_2_VERSION;
    if (config->min_tls_version != 0) {
        if (config->min_tls_version == 0x0303) {
            min_version = TLS1_2_VERSION;
        } else if (config->min_tls_version == 0x0304) {
            min_version = TLS1_3_VERSION;
        }
    }
    SSL_CTX_set_min_proto_version(ctx, min_version);

    /* Load CA certificate for server verification */
    if (config->verify_peer) {
        if (config->ca_cert_path != NULL) {
            if (SSL_CTX_load_verify_locations(ctx, config->ca_cert_path, NULL) != 1) {
                SSL_CTX_free(ctx);
                return NULL;
            }
        } else if (config->ca_cert_buffer != NULL && config->ca_cert_len > 0) {
            /* Load CA from buffer */
            BIO *bio = BIO_new_mem_buf(config->ca_cert_buffer, (int)config->ca_cert_len);
            if (bio == NULL) {
                SSL_CTX_free(ctx);
                return NULL;
            }
            X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
            BIO_free(bio);
            if (cert == NULL) {
                SSL_CTX_free(ctx);
                return NULL;
            }
            X509_STORE *store = SSL_CTX_get_cert_store(ctx);
            X509_STORE_add_cert(store, cert);
            X509_free(cert);
        } else {
            /* Use default CA paths */
            SSL_CTX_set_default_verify_paths(ctx);
        }

        /* Enable peer verification */
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    } else {
        /* Disable peer verification (not recommended for production) */
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }

    /* Load client certificate (for mTLS) */
    if (config->client_cert_path != NULL) {
        if (SSL_CTX_use_certificate_file(ctx, config->client_cert_path,
                                         SSL_FILETYPE_PEM) != 1) {
            SSL_CTX_free(ctx);
            return NULL;
        }
    }

    /* Load client private key (for mTLS) */
    if (config->client_key_path != NULL) {
        if (SSL_CTX_use_PrivateKey_file(ctx, config->client_key_path,
                                        SSL_FILETYPE_PEM) != 1) {
            SSL_CTX_free(ctx);
            return NULL;
        }
        /* Verify private key matches certificate */
        if (SSL_CTX_check_private_key(ctx) != 1) {
            SSL_CTX_free(ctx);
            return NULL;
        }
    }

    return ctx;
}

/**
 * @brief Configure SSL session with ALPN
 */
static mqtt_error_t configure_ssl_alpn(SSL *ssl, const char **alpn_protocols)
{
    if (alpn_protocols == NULL) {
        return MQTT_OK;
    }

    /* Build ALPN wire format: length-prefixed strings */
    size_t total_len = 0;
    size_t count = 0;
    for (const char **p = alpn_protocols; *p != NULL; p++) {
        total_len += 1 + strlen(*p);  /* 1 byte length + string */
        count++;
    }

    if (total_len == 0) {
        return MQTT_OK;
    }

    unsigned char *alpn_buf = malloc(total_len);
    if (alpn_buf == NULL) {
        return MQTT_ERR_NOMEM;
    }

    unsigned char *ptr = alpn_buf;
    for (const char **p = alpn_protocols; *p != NULL; p++) {
        size_t len = strlen(*p);
        *ptr++ = (unsigned char)len;
        memcpy(ptr, *p, len);
        ptr += len;
    }

    int ret = SSL_set_alpn_protos(ssl, alpn_buf, (unsigned int)total_len);
    free(alpn_buf);

    return (ret == 0) ? MQTT_OK : MQTT_ERR_INTERNAL;
}

/**
 * @brief Perform TCP connection with timeout
 */
static mqtt_error_t tcp_connect_with_timeout(mqtt_tls_transport_t *tls,
                                             const char *host, uint16_t port,
                                             uint32_t timeout_ms)
{
    /* Create socket */
    mqtt_error_t err = mqtt_socket_create(&tls->socket);
    if (err != MQTT_OK) {
        return err;
    }

    /* Connect to host with timeout */
    err = mqtt_socket_connect(tls->socket, host, port, timeout_ms);
    if (err != MQTT_OK) {
        mqtt_socket_close(tls->socket);
        tls->socket = MQTT_INVALID_SOCKET;
        return err;
    }

    return MQTT_OK;
}

/**
 * @brief Perform TLS handshake
 */
static mqtt_error_t perform_tls_handshake(mqtt_tls_transport_t *tls)
{
    mqtt_tls_context_t *ctx = tls->tls_ctx;

    /* Clear want flags */
    ctx->want_read = 0;
    ctx->want_write = 0;

    int ret = SSL_connect(ctx->ssl);
    if (ret == 1) {
        /* Handshake complete */
        tls->handshake_state = MQTT_TLS_HANDSHAKE_COMPLETE;

        /* Verify hostname if enabled */
        if (tls->config.verify_hostname && tls->hostname != NULL) {
            X509 *cert = SSL_get_peer_certificate(ctx->ssl);
            if (cert == NULL) {
                tls->handshake_state = MQTT_TLS_HANDSHAKE_FAILED;
                return MQTT_ERR_TLS_HANDSHAKE;
            }

            /* Use OpenSSL's built-in hostname verification */
            int check = X509_check_host(cert, tls->hostname, strlen(tls->hostname),
                                        0, NULL);
            X509_free(cert);

            if (check != 1) {
                tls->handshake_state = MQTT_TLS_HANDSHAKE_FAILED;
                return MQTT_ERR_TLS_HANDSHAKE;
            }
        }

        return MQTT_OK;
    }

    int ssl_error = SSL_get_error(ctx->ssl, ret);
    switch (ssl_error) {
        case SSL_ERROR_WANT_READ:
            ctx->want_read = 1;
            tls->handshake_state = MQTT_TLS_HANDSHAKE_IN_PROGRESS;
            return MQTT_ERR_WOULD_BLOCK;

        case SSL_ERROR_WANT_WRITE:
            ctx->want_write = 1;
            tls->handshake_state = MQTT_TLS_HANDSHAKE_IN_PROGRESS;
            return MQTT_ERR_WOULD_BLOCK;

        default:
            tls->handshake_state = MQTT_TLS_HANDSHAKE_FAILED;
            return MQTT_ERR_TLS_HANDSHAKE;
    }
}

/*******************************************************************************
 * Transport Factory Function
 ******************************************************************************/

mqtt_transport_t *mqtt_transport_tls_create(const mqtt_tls_config_t *config)
{
    if (config == NULL) {
        return NULL;
    }

    /* Ensure OpenSSL is initialized */
    static int ssl_initialized = 0;
    if (!ssl_initialized) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        ssl_initialized = 1;
    }

    mqtt_tls_transport_t *tls = malloc(sizeof(mqtt_tls_transport_t));
    if (tls == NULL) {
        return NULL;
    }

    /* Initialize structure */
    memset(tls, 0, sizeof(mqtt_tls_transport_t));
    tls->base.type       = MQTT_TRANSPORT_TLS;
    tls->base.status     = MQTT_TRANSPORT_DISCONNECTED;
    tls->base.ops        = &tls_ops;
    tls->base.last_error = MQTT_OK;
    tls->socket          = MQTT_INVALID_SOCKET;
    tls->blocking        = true;
    tls->handshake_state = MQTT_TLS_HANDSHAKE_NOT_STARTED;

    /* Copy configuration */
    memcpy(&tls->config, config, sizeof(mqtt_tls_config_t));

    /* Create TLS context */
    tls->tls_ctx = malloc(sizeof(mqtt_tls_context_t));
    if (tls->tls_ctx == NULL) {
        free(tls);
        return NULL;
    }
    memset(tls->tls_ctx, 0, sizeof(mqtt_tls_context_t));

    /* Create SSL context */
    tls->tls_ctx->ssl_ctx = create_ssl_context(config);
    if (tls->tls_ctx->ssl_ctx == NULL) {
        free(tls->tls_ctx);
        free(tls);
        return NULL;
    }

    return &tls->base;
}

/*******************************************************************************
 * TLS Transport Operations Implementation
 ******************************************************************************/

static mqtt_error_t tls_connect(mqtt_transport_t *transport,
                                const char *host, uint16_t port,
                                uint32_t timeout_ms)
{
    if (transport == NULL || host == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_tls_transport_t *tls = (mqtt_tls_transport_t *)transport;

    /* Check if already connected */
    if (tls->socket != MQTT_INVALID_SOCKET) {
        return MQTT_ERR_ALREADY_CONNECTED;
    }

    /* Update status */
    transport->status = MQTT_TRANSPORT_CONNECTING;

    /* Store hostname for SNI and verification */
    if (tls->hostname != NULL) {
        free(tls->hostname);
    }
    tls->hostname = strdup(host);
    if (tls->hostname == NULL) {
        transport->status = MQTT_TRANSPORT_ERROR;
        return MQTT_ERR_NOMEM;
    }

    /* TCP connection */
    mqtt_error_t err = tcp_connect_with_timeout(tls, host, port, timeout_ms);
    if (err != MQTT_OK) {
        transport->status = MQTT_TRANSPORT_ERROR;
        return err;
    }

    /* Create SSL object */
    if (tls->tls_ctx->ssl != NULL) {
        SSL_free(tls->tls_ctx->ssl);
    }
    tls->tls_ctx->ssl = SSL_new(tls->tls_ctx->ssl_ctx);
    if (tls->tls_ctx->ssl == NULL) {
        mqtt_socket_close(tls->socket);
        tls->socket = MQTT_INVALID_SOCKET;
        transport->status = MQTT_TRANSPORT_ERROR;
        return MQTT_ERR_INTERNAL;
    }

    /* Attach socket to SSL */
    if (SSL_set_fd(tls->tls_ctx->ssl, (int)tls->socket) != 1) {
        SSL_free(tls->tls_ctx->ssl);
        tls->tls_ctx->ssl = NULL;
        mqtt_socket_close(tls->socket);
        tls->socket = MQTT_INVALID_SOCKET;
        transport->status = MQTT_TRANSPORT_ERROR;
        return MQTT_ERR_INTERNAL;
    }

    /* Set SNI hostname */
    if (SSL_set_tlsext_host_name(tls->tls_ctx->ssl, tls->hostname) != 1) {
        SSL_free(tls->tls_ctx->ssl);
        tls->tls_ctx->ssl = NULL;
        mqtt_socket_close(tls->socket);
        tls->socket = MQTT_INVALID_SOCKET;
        transport->status = MQTT_TRANSPORT_ERROR;
        return MQTT_ERR_INTERNAL;
    }

    /* Configure ALPN if specified */
    if (tls->config.alpn_protocols != NULL) {
        err = configure_ssl_alpn(tls->tls_ctx->ssl, tls->config.alpn_protocols);
        if (err != MQTT_OK) {
            SSL_free(tls->tls_ctx->ssl);
            tls->tls_ctx->ssl = NULL;
            mqtt_socket_close(tls->socket);
            tls->socket = MQTT_INVALID_SOCKET;
            transport->status = MQTT_TRANSPORT_ERROR;
            return err;
        }
    }

    /* Set socket to non-blocking for handshake if in non-blocking mode */
    if (!tls->blocking) {
        mqtt_socket_set_blocking(tls->socket, false);
    }

    /* Perform TLS handshake */
    tls->handshake_state = MQTT_TLS_HANDSHAKE_IN_PROGRESS;
    err = perform_tls_handshake(tls);

    if (err == MQTT_OK) {
        transport->status = MQTT_TRANSPORT_CONNECTED;
    } else if (err == MQTT_ERR_WOULD_BLOCK) {
        /* Handshake in progress (non-blocking mode) */
        return err;
    } else {
        SSL_free(tls->tls_ctx->ssl);
        tls->tls_ctx->ssl = NULL;
        mqtt_socket_close(tls->socket);
        tls->socket = MQTT_INVALID_SOCKET;
        transport->status = MQTT_TRANSPORT_ERROR;
    }

    return err;
}

static mqtt_error_t tls_disconnect(mqtt_transport_t *transport)
{
    if (transport == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_tls_transport_t *tls = (mqtt_tls_transport_t *)transport;

    /* Send SSL shutdown (graceful) */
    if (tls->tls_ctx != NULL && tls->tls_ctx->ssl != NULL) {
        /* Don't wait for peer's close_notify in non-blocking mode */
        SSL_set_shutdown(tls->tls_ctx->ssl, SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);
        SSL_shutdown(tls->tls_ctx->ssl);
        SSL_free(tls->tls_ctx->ssl);
        tls->tls_ctx->ssl = NULL;
    }

    /* Close underlying socket */
    if (tls->socket != MQTT_INVALID_SOCKET) {
        mqtt_socket_close(tls->socket);
        tls->socket = MQTT_INVALID_SOCKET;
    }

    tls->handshake_state = MQTT_TLS_HANDSHAKE_NOT_STARTED;
    transport->status = MQTT_TRANSPORT_DISCONNECTED;

    return MQTT_OK;
}

static ssize_t tls_send(mqtt_transport_t *transport,
                        const void *buf, size_t len)
{
    if (transport == NULL || buf == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_tls_transport_t *tls = (mqtt_tls_transport_t *)transport;

    /* Check if connected */
    if (tls->socket == MQTT_INVALID_SOCKET || tls->tls_ctx->ssl == NULL) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    /* Check handshake state */
    if (tls->handshake_state != MQTT_TLS_HANDSHAKE_COMPLETE) {
        return MQTT_ERR_INVALID_STATE;
    }

    /* Clear want flags */
    tls->tls_ctx->want_read = 0;
    tls->tls_ctx->want_write = 0;

    int result = SSL_write(tls->tls_ctx->ssl, buf, (int)len);
    if (result > 0) {
        return result;
    }

    mqtt_error_t err = convert_ssl_error(tls->tls_ctx->ssl, result);
    if (err == MQTT_ERR_WOULD_BLOCK) {
        int ssl_error = SSL_get_error(tls->tls_ctx->ssl, result);
        if (ssl_error == SSL_ERROR_WANT_READ) {
            tls->tls_ctx->want_read = 1;
        } else if (ssl_error == SSL_ERROR_WANT_WRITE) {
            tls->tls_ctx->want_write = 1;
        }
    } else if (err != MQTT_OK) {
        transport->status = MQTT_TRANSPORT_ERROR;
    }

    return err;
}

static ssize_t tls_recv(mqtt_transport_t *transport,
                        void *buf, size_t len)
{
    if (transport == NULL || buf == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_tls_transport_t *tls = (mqtt_tls_transport_t *)transport;

    /* Check if connected */
    if (tls->socket == MQTT_INVALID_SOCKET || tls->tls_ctx->ssl == NULL) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    /* Check handshake state */
    if (tls->handshake_state != MQTT_TLS_HANDSHAKE_COMPLETE) {
        return MQTT_ERR_INVALID_STATE;
    }

    /* Clear want flags */
    tls->tls_ctx->want_read = 0;
    tls->tls_ctx->want_write = 0;

    int result = SSL_read(tls->tls_ctx->ssl, buf, (int)len);
    if (result > 0) {
        return result;
    }

    mqtt_error_t err = convert_ssl_error(tls->tls_ctx->ssl, result);
    if (err == MQTT_ERR_WOULD_BLOCK) {
        int ssl_error = SSL_get_error(tls->tls_ctx->ssl, result);
        if (ssl_error == SSL_ERROR_WANT_READ) {
            tls->tls_ctx->want_read = 1;
        } else if (ssl_error == SSL_ERROR_WANT_WRITE) {
            tls->tls_ctx->want_write = 1;
        }
    } else if (err != MQTT_OK) {
        transport->status = MQTT_TRANSPORT_ERROR;
    }

    return err;
}

static int tls_get_fd(mqtt_transport_t *transport)
{
    if (transport == NULL) {
        return -1;
    }

    mqtt_tls_transport_t *tls = (mqtt_tls_transport_t *)transport;

    if (tls->socket == MQTT_INVALID_SOCKET) {
        return -1;
    }

    return (int)tls->socket;
}

static mqtt_error_t tls_set_blocking(mqtt_transport_t *transport, bool blocking)
{
    if (transport == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_tls_transport_t *tls = (mqtt_tls_transport_t *)transport;

    /* Update blocking mode flag */
    tls->blocking = blocking;

    /* If socket is open, apply the blocking mode immediately */
    if (tls->socket != MQTT_INVALID_SOCKET) {
        mqtt_error_t err = mqtt_socket_set_blocking(tls->socket, blocking);
        if (err != MQTT_OK) {
            return err;
        }
    }

    return MQTT_OK;
}

static void tls_destroy(mqtt_transport_t *transport)
{
    if (transport == NULL) {
        return;
    }

    mqtt_tls_transport_t *tls = (mqtt_tls_transport_t *)transport;

    /* Disconnect if needed */
    if (tls->socket != MQTT_INVALID_SOCKET) {
        tls_disconnect(transport);
    }

    /* Free TLS context */
    if (tls->tls_ctx != NULL) {
        if (tls->tls_ctx->ssl != NULL) {
            SSL_free(tls->tls_ctx->ssl);
        }
        if (tls->tls_ctx->ssl_ctx != NULL) {
            SSL_CTX_free(tls->tls_ctx->ssl_ctx);
        }
        free(tls->tls_ctx);
    }

    /* Free hostname */
    if (tls->hostname != NULL) {
        free(tls->hostname);
    }

    /* Free the transport structure */
    free(tls);
}

/*******************************************************************************
 * TLS Handshake Helper Functions
 ******************************************************************************/

mqtt_error_t mqtt_tls_continue_handshake(mqtt_transport_t *transport)
{
    if (transport == NULL || transport->type != MQTT_TRANSPORT_TLS) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_tls_transport_t *tls = (mqtt_tls_transport_t *)transport;

    if (tls->handshake_state == MQTT_TLS_HANDSHAKE_COMPLETE) {
        return MQTT_OK;
    }

    if (tls->handshake_state == MQTT_TLS_HANDSHAKE_FAILED) {
        return MQTT_ERR_TLS_HANDSHAKE;
    }

    mqtt_error_t err = perform_tls_handshake(tls);
    if (err == MQTT_OK) {
        transport->status = MQTT_TRANSPORT_CONNECTED;
    }

    return err;
}

bool mqtt_tls_handshake_complete(mqtt_transport_t *transport)
{
    if (transport == NULL || transport->type != MQTT_TRANSPORT_TLS) {
        return false;
    }

    mqtt_tls_transport_t *tls = (mqtt_tls_transport_t *)transport;
    return tls->handshake_state == MQTT_TLS_HANDSHAKE_COMPLETE;
}

bool mqtt_tls_want_read(mqtt_transport_t *transport)
{
    if (transport == NULL || transport->type != MQTT_TRANSPORT_TLS) {
        return false;
    }

    mqtt_tls_transport_t *tls = (mqtt_tls_transport_t *)transport;
    return tls->tls_ctx != NULL && tls->tls_ctx->want_read;
}

bool mqtt_tls_want_write(mqtt_transport_t *transport)
{
    if (transport == NULL || transport->type != MQTT_TRANSPORT_TLS) {
        return false;
    }

    mqtt_tls_transport_t *tls = (mqtt_tls_transport_t *)transport;
    return tls->tls_ctx != NULL && tls->tls_ctx->want_write;
}

#else /* !MQTT_ENABLE_TLS */

/* Stub implementations when TLS is disabled */
mqtt_transport_t *mqtt_transport_tls_create(const mqtt_tls_config_t *config)
{
    (void)config;
    return NULL;
}

const mqtt_transport_ops_t *mqtt_tls_get_ops(void)
{
    return NULL;
}

mqtt_error_t mqtt_tls_continue_handshake(mqtt_transport_t *transport)
{
    (void)transport;
    return MQTT_ERR_NOT_IMPLEMENTED;
}

bool mqtt_tls_handshake_complete(mqtt_transport_t *transport)
{
    (void)transport;
    return false;
}

bool mqtt_tls_want_read(mqtt_transport_t *transport)
{
    (void)transport;
    return false;
}

bool mqtt_tls_want_write(mqtt_transport_t *transport)
{
    (void)transport;
    return false;
}

#endif /* MQTT_ENABLE_TLS */
