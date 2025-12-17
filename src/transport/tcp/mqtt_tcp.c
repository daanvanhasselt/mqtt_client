/**
 * @file mqtt_tcp.c
 * @brief TCP Transport Implementation
 *
 * This file implements the TCP transport layer for MQTT connections.
 * It provides concrete implementations of all transport operations for
 * plain TCP sockets, using the platform abstraction layer for socket
 * operations.
 *
 * The implementation:
 * - Manages TCP socket lifecycle
 * - Handles connection establishment with timeout support
 * - Provides send/recv operations with error handling
 * - Supports blocking and non-blocking modes
 * - Converts platform errors to MQTT error codes
 */

#include "mqtt_tcp.h"
#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 * Forward Declarations - TCP Transport Operations
 ******************************************************************************/

static mqtt_error_t tcp_connect(mqtt_transport_t *transport,
                                const char *host, uint16_t port,
                                uint32_t timeout_ms);
static mqtt_error_t tcp_disconnect(mqtt_transport_t *transport);
static ssize_t      tcp_send(mqtt_transport_t *transport,
                             const void *buf, size_t len);
static ssize_t      tcp_recv(mqtt_transport_t *transport,
                             void *buf, size_t len);
static int          tcp_get_fd(mqtt_transport_t *transport);
static mqtt_error_t tcp_set_blocking(mqtt_transport_t *transport, bool blocking);
static void         tcp_destroy(mqtt_transport_t *transport);

/*******************************************************************************
 * TCP Transport Operations Vtable
 ******************************************************************************/

static const mqtt_transport_ops_t tcp_ops = {
    .connect      = tcp_connect,
    .disconnect   = tcp_disconnect,
    .send         = tcp_send,
    .recv         = tcp_recv,
    .get_fd       = tcp_get_fd,
    .set_blocking = tcp_set_blocking,
    .destroy      = tcp_destroy
};

const mqtt_transport_ops_t *mqtt_tcp_get_ops(void)
{
    return &tcp_ops;
}

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

/**
 * @brief Convert platform socket error to MQTT error code
 *
 * Maps platform-specific error codes to standardized MQTT error codes.
 *
 * @param platform_err Platform error code (from mqtt_socket_get_error())
 * @return Corresponding MQTT error code
 */
__attribute__((unused))
static mqtt_error_t convert_socket_error(int platform_err)
{
    (void)platform_err;  /* Platform error codes vary by system */

    /* For now, return a generic socket error */
    /* TODO: Implement platform-specific error mapping */
    return MQTT_ERR_SOCKET;
}

/*******************************************************************************
 * Transport Factory Function
 ******************************************************************************/

mqtt_transport_t *mqtt_transport_tcp_create(void)
{
    mqtt_tcp_transport_t *tcp_transport = malloc(sizeof(mqtt_tcp_transport_t));
    if (tcp_transport == NULL) {
        return NULL;
    }

    /* Initialize base transport */
    memset(tcp_transport, 0, sizeof(mqtt_tcp_transport_t));
    tcp_transport->base.type       = MQTT_TRANSPORT_TCP;
    tcp_transport->base.status     = MQTT_TRANSPORT_DISCONNECTED;
    tcp_transport->base.ops        = &tcp_ops;
    tcp_transport->base.last_error = MQTT_OK;

    /* Initialize TCP-specific fields */
    tcp_transport->socket   = MQTT_INVALID_SOCKET;
    tcp_transport->blocking = true;

    return &tcp_transport->base;
}

/*******************************************************************************
 * TCP Transport Operations Implementation
 ******************************************************************************/

static mqtt_error_t tcp_connect(mqtt_transport_t *transport,
                                const char *host, uint16_t port,
                                uint32_t timeout_ms)
{
    if (transport == NULL || host == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_tcp_transport_t *tcp = (mqtt_tcp_transport_t *)transport;

    /* Check if already connected */
    if (tcp->socket != MQTT_INVALID_SOCKET) {
        return MQTT_ERR_ALREADY_CONNECTED;
    }

    /* Update status */
    transport->status = MQTT_TRANSPORT_CONNECTING;

    /* Create socket */
    mqtt_error_t err = mqtt_socket_create(&tcp->socket);
    if (err != MQTT_OK) {
        transport->status = MQTT_TRANSPORT_ERROR;
        return err;
    }

    /* Connect to host with timeout */
    err = mqtt_socket_connect(tcp->socket, host, port, timeout_ms);
    if (err != MQTT_OK) {
        mqtt_socket_close(tcp->socket);
        tcp->socket = MQTT_INVALID_SOCKET;
        transport->status = MQTT_TRANSPORT_ERROR;
        return err;
    }

    /* Set initial blocking mode (default is blocking) */
    err = mqtt_socket_set_blocking(tcp->socket, tcp->blocking);
    if (err != MQTT_OK) {
        mqtt_socket_close(tcp->socket);
        tcp->socket = MQTT_INVALID_SOCKET;
        transport->status = MQTT_TRANSPORT_ERROR;
        return err;
    }

    /* Connection successful */
    transport->status = MQTT_TRANSPORT_CONNECTED;
    return MQTT_OK;
}

static mqtt_error_t tcp_disconnect(mqtt_transport_t *transport)
{
    if (transport == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_tcp_transport_t *tcp = (mqtt_tcp_transport_t *)transport;

    /* Check if already disconnected */
    if (tcp->socket == MQTT_INVALID_SOCKET) {
        transport->status = MQTT_TRANSPORT_DISCONNECTED;
        return MQTT_OK;
    }

    /* Close socket */
    mqtt_error_t err = mqtt_socket_close(tcp->socket);
    tcp->socket = MQTT_INVALID_SOCKET;
    transport->status = MQTT_TRANSPORT_DISCONNECTED;

    return err;
}

static ssize_t tcp_send(mqtt_transport_t *transport,
                        const void *buf, size_t len)
{
    if (transport == NULL || buf == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_tcp_transport_t *tcp = (mqtt_tcp_transport_t *)transport;

    /* Check if connected */
    if (tcp->socket == MQTT_INVALID_SOCKET) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    /* Send data through socket */
    ssize_t result = mqtt_socket_send(tcp->socket, buf, len);

    /* Handle errors */
    if (result < 0) {
        /* Check for specific error conditions */
        if (result == MQTT_ERR_WOULD_BLOCK) {
            /* Non-blocking socket, no data sent */
            return result;
        }

        /* Connection may be lost */
        transport->status = MQTT_TRANSPORT_ERROR;
        return result;
    }

    return result;
}

static ssize_t tcp_recv(mqtt_transport_t *transport,
                        void *buf, size_t len)
{
    if (transport == NULL || buf == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_tcp_transport_t *tcp = (mqtt_tcp_transport_t *)transport;

    /* Check if connected */
    if (tcp->socket == MQTT_INVALID_SOCKET) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    /* Receive data from socket */
    ssize_t result = mqtt_socket_recv(tcp->socket, buf, len);

    /* Handle errors and connection close */
    if (result < 0) {
        /* Check for specific error conditions */
        if (result == MQTT_ERR_WOULD_BLOCK) {
            /* Non-blocking socket, no data available */
            return result;
        }

        /* Connection may be lost */
        transport->status = MQTT_TRANSPORT_ERROR;
        return result;
    } else if (result == 0) {
        /* Connection closed by peer */
        transport->status = MQTT_TRANSPORT_DISCONNECTED;
        return MQTT_ERR_CONNECTION_LOST;
    }

    return result;
}

static int tcp_get_fd(mqtt_transport_t *transport)
{
    if (transport == NULL) {
        return -1;
    }

    mqtt_tcp_transport_t *tcp = (mqtt_tcp_transport_t *)transport;

    /* Return socket fd or -1 if invalid */
    if (tcp->socket == MQTT_INVALID_SOCKET) {
        return -1;
    }

    /* On POSIX, socket is already an int fd */
    /* On Windows, SOCKET is UINT_PTR, but we cast it */
    return (int)tcp->socket;
}

static mqtt_error_t tcp_set_blocking(mqtt_transport_t *transport, bool blocking)
{
    if (transport == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_tcp_transport_t *tcp = (mqtt_tcp_transport_t *)transport;

    /* Update blocking mode flag */
    tcp->blocking = blocking;

    /* If socket is open, apply the blocking mode immediately */
    if (tcp->socket != MQTT_INVALID_SOCKET) {
        mqtt_error_t err = mqtt_socket_set_blocking(tcp->socket, blocking);
        if (err != MQTT_OK) {
            return err;
        }
    }

    return MQTT_OK;
}

static void tcp_destroy(mqtt_transport_t *transport)
{
    if (transport == NULL) {
        return;
    }

    mqtt_tcp_transport_t *tcp = (mqtt_tcp_transport_t *)transport;

    /* Ensure socket is closed before freeing */
    if (tcp->socket != MQTT_INVALID_SOCKET) {
        mqtt_socket_close(tcp->socket);
        tcp->socket = MQTT_INVALID_SOCKET;
    }

    /* Free the transport structure */
    free(tcp);
}
