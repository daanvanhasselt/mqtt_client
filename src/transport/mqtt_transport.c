/**
 * @file mqtt_transport.c
 * @brief Generic Transport Dispatcher Implementation
 *
 * This file implements the generic transport operations that dispatch calls
 * through the vtable to the concrete transport implementations. It provides
 * a uniform interface for all transport types.
 */

#include "mqtt_transport.h"
#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 * Transport Factory Functions
 ******************************************************************************/

mqtt_transport_t *mqtt_transport_create(mqtt_transport_type_t type,
                                        const mqtt_tls_config_t *tls_config,
                                        const mqtt_ws_config_t *ws_config)
{
    (void)tls_config;  /* Unused for now (TLS not implemented) */
    (void)ws_config;  /* Unused for now (WebSocket not implemented) */

    switch (type) {
        case MQTT_TRANSPORT_TCP:
            return mqtt_transport_tcp_create();

        case MQTT_TRANSPORT_TLS:
#ifdef MQTT_ENABLE_TLS
            if (tls_config == NULL) {
                return NULL;
            }
            /* TODO: Implement TLS transport creation */
            return NULL;
#else
            return NULL;
#endif

        case MQTT_TRANSPORT_WS:
#ifdef MQTT_ENABLE_WEBSOCKET
            /* TODO: Implement WebSocket transport creation */
            return NULL;
#else
            return NULL;
#endif

        case MQTT_TRANSPORT_WSS:
#if defined(MQTT_ENABLE_TLS) && defined(MQTT_ENABLE_WEBSOCKET)
            if (tls_config == NULL) {
                return NULL;
            }
            /* TODO: Implement WebSocket Secure transport creation */
            return NULL;
#else
            return NULL;
#endif

        default:
            return NULL;
    }
}

/*******************************************************************************
 * Generic Transport Operations - Vtable Dispatch
 ******************************************************************************/

mqtt_error_t mqtt_transport_connect(mqtt_transport_t *transport,
                                    const char *host, uint16_t port,
                                    uint32_t timeout_ms)
{
    if (transport == NULL || transport->ops == NULL || transport->ops->connect == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (host == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_error_t err = transport->ops->connect(transport, host, port, timeout_ms);
    if (err != MQTT_OK) {
        transport->last_error = err;
        transport->status = MQTT_TRANSPORT_ERROR;
    } else {
        transport->status = MQTT_TRANSPORT_CONNECTED;
    }

    return err;
}

mqtt_error_t mqtt_transport_disconnect(mqtt_transport_t *transport)
{
    if (transport == NULL || transport->ops == NULL || transport->ops->disconnect == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_error_t err = transport->ops->disconnect(transport);
    if (err == MQTT_OK) {
        transport->status = MQTT_TRANSPORT_DISCONNECTED;
    } else {
        transport->last_error = err;
        transport->status = MQTT_TRANSPORT_ERROR;
    }

    return err;
}

ssize_t mqtt_transport_send(mqtt_transport_t *transport, const void *buf, size_t len)
{
    if (transport == NULL || transport->ops == NULL || transport->ops->send == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (buf == NULL && len > 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    ssize_t result = transport->ops->send(transport, buf, len);
    if (result < 0) {
        transport->last_error = (mqtt_error_t)result;
    }

    return result;
}

ssize_t mqtt_transport_recv(mqtt_transport_t *transport, void *buf, size_t len)
{
    if (transport == NULL || transport->ops == NULL || transport->ops->recv == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (buf == NULL && len > 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    ssize_t result = transport->ops->recv(transport, buf, len);
    if (result < 0) {
        transport->last_error = (mqtt_error_t)result;
    }

    return result;
}

int mqtt_transport_get_fd(mqtt_transport_t *transport)
{
    if (transport == NULL || transport->ops == NULL || transport->ops->get_fd == NULL) {
        return -1;
    }

    return transport->ops->get_fd(transport);
}

mqtt_error_t mqtt_transport_set_blocking(mqtt_transport_t *transport, bool blocking)
{
    if (transport == NULL || transport->ops == NULL || transport->ops->set_blocking == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_error_t err = transport->ops->set_blocking(transport, blocking);
    if (err != MQTT_OK) {
        transport->last_error = err;
    }

    return err;
}

void mqtt_transport_destroy(mqtt_transport_t *transport)
{
    if (transport == NULL) {
        return;
    }

    if (transport->ops != NULL && transport->ops->destroy != NULL) {
        transport->ops->destroy(transport);
    }
}

mqtt_transport_status_t mqtt_transport_get_status(mqtt_transport_t *transport)
{
    if (transport == NULL) {
        return MQTT_TRANSPORT_ERROR;
    }

    return transport->status;
}
