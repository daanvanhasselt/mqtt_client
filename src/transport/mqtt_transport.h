/**
 * @file mqtt_transport.h
 * @brief Transport Abstraction Layer for MQTT Client
 *
 * This file defines the transport abstraction interface for the MQTT client library.
 * It provides a pluggable transport layer that supports TCP, TLS, WebSocket, and
 * WebSocket Secure transports through a vtable-based dispatch mechanism.
 *
 * The transport layer handles:
 * - Connection establishment and teardown
 * - Bi-directional data transfer (send/recv)
 * - Socket file descriptor management
 * - Blocking/non-blocking mode control
 *
 * All transport implementations must implement the operations defined in
 * mqtt_transport_ops_t and embed mqtt_transport_t as their first member.
 */

#ifndef MQTT_TRANSPORT_H
#define MQTT_TRANSPORT_H

#include "mqtt/mqtt_types.h"
#include "mqtt/mqtt_error.h"
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Transport Status
 ******************************************************************************/

/**
 * @brief Transport connection status
 */
typedef enum mqtt_transport_status {
    MQTT_TRANSPORT_DISCONNECTED,  /**< Transport is disconnected */
    MQTT_TRANSPORT_CONNECTING,    /**< Transport is connecting */
    MQTT_TRANSPORT_CONNECTED,     /**< Transport is connected */
    MQTT_TRANSPORT_ERROR          /**< Transport is in error state */
} mqtt_transport_status_t;

/*******************************************************************************
 * Transport Structure and Operations
 ******************************************************************************/

/* Forward declaration */
typedef struct mqtt_transport mqtt_transport_t;

/**
 * @brief Transport operations vtable
 *
 * This structure contains function pointers for all transport operations.
 * Each transport implementation provides its own vtable with concrete
 * implementations of these operations.
 */
typedef struct mqtt_transport_ops {
    /**
     * @brief Connect to remote host
     *
     * @param transport Transport instance
     * @param host Hostname or IP address
     * @param port Port number
     * @param timeout_ms Connection timeout in milliseconds (0 = blocking)
     * @return MQTT_OK on success, error code otherwise
     */
    mqtt_error_t (*connect)(mqtt_transport_t *transport,
                            const char *host, uint16_t port,
                            uint32_t timeout_ms);

    /**
     * @brief Disconnect from remote host
     *
     * @param transport Transport instance
     * @return MQTT_OK on success, error code otherwise
     */
    mqtt_error_t (*disconnect)(mqtt_transport_t *transport);

    /**
     * @brief Send data through transport
     *
     * @param transport Transport instance
     * @param buf Buffer containing data to send
     * @param len Number of bytes to send
     * @return Number of bytes sent on success, negative error code on failure
     */
    ssize_t      (*send)(mqtt_transport_t *transport,
                         const void *buf, size_t len);

    /**
     * @brief Receive data from transport
     *
     * @param transport Transport instance
     * @param buf Buffer to store received data
     * @param len Maximum number of bytes to receive
     * @return Number of bytes received on success, 0 on connection close,
     *         negative error code on failure
     */
    ssize_t      (*recv)(mqtt_transport_t *transport,
                         void *buf, size_t len);

    /**
     * @brief Get underlying file descriptor
     *
     * @param transport Transport instance
     * @return File descriptor, or -1 if not available
     */
    int          (*get_fd)(mqtt_transport_t *transport);

    /**
     * @brief Set blocking mode
     *
     * @param transport Transport instance
     * @param blocking true for blocking mode, false for non-blocking
     * @return MQTT_OK on success, error code otherwise
     */
    mqtt_error_t (*set_blocking)(mqtt_transport_t *transport, bool blocking);

    /**
     * @brief Destroy transport and free resources
     *
     * @param transport Transport instance
     */
    void         (*destroy)(mqtt_transport_t *transport);
} mqtt_transport_ops_t;

/**
 * @brief Base transport structure
 *
 * This structure must be the first member of any concrete transport
 * implementation to allow safe casting between base and derived types.
 */
struct mqtt_transport {
    mqtt_transport_type_t   type;         /**< Transport type (TCP, TLS, WS, WSS) */
    mqtt_transport_status_t status;       /**< Current connection status */
    const mqtt_transport_ops_t *ops;      /**< Operations vtable */
    mqtt_error_t            last_error;   /**< Last error encountered */
};

/*******************************************************************************
 * Transport Factory Functions
 ******************************************************************************/

/**
 * @brief Create a TCP transport instance
 *
 * Creates a plain TCP transport suitable for unencrypted MQTT connections.
 *
 * @return Transport instance on success, NULL on failure
 *
 * @note The caller must destroy the transport using mqtt_transport_destroy()
 *       when done.
 */
mqtt_transport_t *mqtt_transport_tcp_create(void);

/**
 * @brief Create a WebSocket transport instance
 *
 * Creates a WebSocket transport that wraps an underlying TCP or TLS transport.
 * The underlying transport must already be created but not connected.
 *
 * @param underlying Underlying TCP or TLS transport (ownership transferred)
 * @param config WebSocket configuration (can be NULL for defaults)
 * @return Transport instance on success, NULL on failure
 *
 * @note The caller must destroy the transport using mqtt_transport_destroy()
 *       when done. This will also destroy the underlying transport.
 */
mqtt_transport_t *mqtt_transport_ws_create(mqtt_transport_t *underlying,
                                            const mqtt_ws_config_t *config);

/**
 * @brief Create a transport instance of specified type
 *
 * This is a generic factory function that creates transports based on type.
 * For TLS and WebSocket variants, additional configuration must be provided.
 *
 * @param type Transport type to create
 * @param tls_config TLS configuration (required for TLS/WSS, NULL otherwise)
 * @param ws_config WebSocket configuration (optional for WS/WSS)
 * @return Transport instance on success, NULL on failure
 *
 * @note The caller must destroy the transport using mqtt_transport_destroy()
 *       when done.
 */
mqtt_transport_t *mqtt_transport_create(mqtt_transport_type_t type,
                                        const mqtt_tls_config_t *tls_config,
                                        const mqtt_ws_config_t *ws_config);

/*******************************************************************************
 * Generic Transport Operations
 ******************************************************************************/

/**
 * @brief Connect transport to remote host
 *
 * Dispatches the connect operation through the transport's vtable.
 *
 * @param transport Transport instance
 * @param host Hostname or IP address
 * @param port Port number
 * @param timeout_ms Connection timeout in milliseconds (0 = blocking)
 * @return MQTT_OK on success, error code otherwise
 */
mqtt_error_t mqtt_transport_connect(mqtt_transport_t *transport,
                                    const char *host, uint16_t port,
                                    uint32_t timeout_ms);

/**
 * @brief Disconnect transport
 *
 * Dispatches the disconnect operation through the transport's vtable.
 *
 * @param transport Transport instance
 * @return MQTT_OK on success, error code otherwise
 */
mqtt_error_t mqtt_transport_disconnect(mqtt_transport_t *transport);

/**
 * @brief Send data through transport
 *
 * Dispatches the send operation through the transport's vtable.
 *
 * @param transport Transport instance
 * @param buf Buffer containing data to send
 * @param len Number of bytes to send
 * @return Number of bytes sent on success, negative error code on failure
 */
ssize_t mqtt_transport_send(mqtt_transport_t *transport, const void *buf, size_t len);

/**
 * @brief Receive data from transport
 *
 * Dispatches the recv operation through the transport's vtable.
 *
 * @param transport Transport instance
 * @param buf Buffer to store received data
 * @param len Maximum number of bytes to receive
 * @return Number of bytes received on success, 0 on connection close,
 *         negative error code on failure
 */
ssize_t mqtt_transport_recv(mqtt_transport_t *transport, void *buf, size_t len);

/**
 * @brief Get underlying file descriptor
 *
 * Dispatches the get_fd operation through the transport's vtable.
 *
 * @param transport Transport instance
 * @return File descriptor, or -1 if not available
 */
int mqtt_transport_get_fd(mqtt_transport_t *transport);

/**
 * @brief Set transport blocking mode
 *
 * Dispatches the set_blocking operation through the transport's vtable.
 *
 * @param transport Transport instance
 * @param blocking true for blocking mode, false for non-blocking
 * @return MQTT_OK on success, error code otherwise
 */
mqtt_error_t mqtt_transport_set_blocking(mqtt_transport_t *transport, bool blocking);

/**
 * @brief Destroy transport and free resources
 *
 * Dispatches the destroy operation through the transport's vtable.
 * The transport pointer becomes invalid after this call.
 *
 * @param transport Transport instance
 */
void mqtt_transport_destroy(mqtt_transport_t *transport);

/**
 * @brief Get current transport status
 *
 * @param transport Transport instance
 * @return Current transport status
 */
mqtt_transport_status_t mqtt_transport_get_status(mqtt_transport_t *transport);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_TRANSPORT_H */
