/**
 * @file mqtt_tls_mbedtls.c
 * @brief TLS Transport Implementation using mbedTLS
 *
 * This file provides a stub implementation for mbedTLS backend.
 * Full implementation is planned for a future release.
 */

#include "mqtt_tls.h"
#include "mqtt/mqtt_config.h"

#ifdef MQTT_ENABLE_TLS

/* mbedTLS backend not yet implemented - stub only */

mqtt_transport_t *mqtt_transport_tls_create(const mqtt_tls_config_t *config)
{
    (void)config;
    /* mbedTLS backend not yet implemented */
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
