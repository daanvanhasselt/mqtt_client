/**
 * @file mqtt_tls.h
 * @brief TLS Transport Implementation Header
 *
 * This file defines the TLS-specific transport structure and internal functions
 * for secure MQTT connections. The TLS transport wraps a TCP connection with
 * SSL/TLS encryption using pluggable backends (OpenSSL, mbedTLS).
 *
 * The TLS transport:
 * - Manages SSL/TLS context and session lifecycle
 * - Handles TLS handshake with non-blocking support
 * - Provides encrypted send/recv operations
 * - Supports certificate verification, SNI, and ALPN
 */

#ifndef MQTT_TLS_H
#define MQTT_TLS_H

#include "../mqtt_transport.h"
#include "../../platform/mqtt_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * TLS Handshake State
 ******************************************************************************/

/**
 * @brief TLS handshake state for non-blocking operations
 */
typedef enum mqtt_tls_handshake_state {
    MQTT_TLS_HANDSHAKE_NOT_STARTED,  /**< Handshake not yet started */
    MQTT_TLS_HANDSHAKE_IN_PROGRESS,  /**< Handshake in progress (want read/write) */
    MQTT_TLS_HANDSHAKE_COMPLETE,     /**< Handshake completed successfully */
    MQTT_TLS_HANDSHAKE_FAILED        /**< Handshake failed */
} mqtt_tls_handshake_state_t;

/*******************************************************************************
 * TLS Backend Interface
 ******************************************************************************/

/* Forward declaration of backend-specific context */
typedef struct mqtt_tls_context mqtt_tls_context_t;

/*******************************************************************************
 * TLS Transport Structure
 ******************************************************************************/

/**
 * @brief TLS transport structure
 *
 * This structure extends the base mqtt_transport_t with TLS-specific members.
 * The base transport structure MUST be the first member to allow safe casting
 * between base and derived types.
 */
typedef struct mqtt_tls_transport {
    mqtt_transport_t            base;               /**< Base transport (MUST be first member) */
    mqtt_socket_t               socket;             /**< Underlying TCP socket handle */
    bool                        blocking;           /**< Current blocking mode */
    mqtt_tls_context_t         *tls_ctx;            /**< TLS context (backend-specific) */
    mqtt_tls_handshake_state_t  handshake_state;    /**< Current handshake state */
    mqtt_tls_config_t           config;             /**< TLS configuration copy */
    char                       *hostname;           /**< Hostname for SNI (allocated copy) */
} mqtt_tls_transport_t;

/*******************************************************************************
 * TLS Transport Factory Function
 ******************************************************************************/

/**
 * @brief Create a TLS transport instance
 *
 * Creates a TLS transport suitable for encrypted MQTT connections.
 *
 * @param config TLS configuration (required)
 * @return Transport instance on success, NULL on failure
 *
 * @note The caller must destroy the transport using mqtt_transport_destroy()
 *       when done.
 */
mqtt_transport_t *mqtt_transport_tls_create(const mqtt_tls_config_t *config);

/*******************************************************************************
 * Internal TLS Transport Functions
 ******************************************************************************/

/**
 * @brief Get TLS transport operations vtable
 *
 * Returns a pointer to the static vtable containing TLS transport operations.
 * This is used internally during TLS transport creation.
 *
 * @return Pointer to TLS transport operations vtable
 */
const mqtt_transport_ops_t *mqtt_tls_get_ops(void);

/**
 * @brief Continue TLS handshake (for non-blocking mode)
 *
 * This function should be called when the socket is ready for I/O
 * after a previous connect or handshake returned MQTT_ERR_WOULD_BLOCK.
 *
 * @param transport TLS transport instance
 * @return MQTT_OK if handshake complete, MQTT_ERR_WOULD_BLOCK if more I/O needed,
 *         or error code on failure
 */
mqtt_error_t mqtt_tls_continue_handshake(mqtt_transport_t *transport);

/**
 * @brief Check if TLS handshake is complete
 *
 * @param transport TLS transport instance
 * @return true if handshake is complete, false otherwise
 */
bool mqtt_tls_handshake_complete(mqtt_transport_t *transport);

/**
 * @brief Check if TLS wants to read during handshake
 *
 * @param transport TLS transport instance
 * @return true if TLS layer wants to read
 */
bool mqtt_tls_want_read(mqtt_transport_t *transport);

/**
 * @brief Check if TLS wants to write during handshake
 *
 * @param transport TLS transport instance
 * @return true if TLS layer wants to write
 */
bool mqtt_tls_want_write(mqtt_transport_t *transport);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_TLS_H */
