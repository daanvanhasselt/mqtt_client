/**
 * @file mqtt_tcp.h
 * @brief TCP Transport Implementation Header
 *
 * This file defines the TCP-specific transport structure and internal functions
 * for plain TCP MQTT connections. The TCP transport is the base transport layer
 * used by all MQTT connections and provides fundamental socket operations.
 *
 * The TCP transport:
 * - Creates and manages TCP sockets
 * - Handles connection establishment with timeout support
 * - Provides blocking and non-blocking I/O
 * - Manages socket lifecycle (open, close)
 */

#ifndef MQTT_TCP_H
#define MQTT_TCP_H

#include "../mqtt_transport.h"
#include "../../platform/mqtt_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * TCP Transport Structure
 ******************************************************************************/

/**
 * @brief TCP transport structure
 *
 * This structure extends the base mqtt_transport_t with TCP-specific members.
 * The base transport structure MUST be the first member to allow safe casting
 * between base and derived types.
 */
typedef struct mqtt_tcp_transport {
    mqtt_transport_t base;      /**< Base transport (MUST be first member) */
    mqtt_socket_t    socket;    /**< TCP socket handle */
    bool             blocking;  /**< Current blocking mode */
} mqtt_tcp_transport_t;

/*******************************************************************************
 * Internal TCP Transport Functions
 ******************************************************************************/

/**
 * @brief Initialize TCP transport operations vtable
 *
 * Returns a pointer to the static vtable containing TCP transport operations.
 * This is used internally during TCP transport creation.
 *
 * @return Pointer to TCP transport operations vtable
 */
const mqtt_transport_ops_t *mqtt_tcp_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_TCP_H */
