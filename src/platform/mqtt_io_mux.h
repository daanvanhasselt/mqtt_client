/**
 * @file mqtt_io_mux.h
 * @brief I/O Multiplexer Abstraction Layer
 *
 * This file defines the I/O multiplexer interface for the MQTT client library.
 * It provides a consistent API for event-driven I/O across different platforms:
 *
 * - Linux: epoll (high performance)
 * - macOS/BSD: kqueue (high performance)
 * - Windows: select or IOCP (future)
 * - POSIX fallback: poll()
 *
 * The multiplexer allows monitoring multiple file descriptors for read/write
 * readiness without blocking on individual operations.
 */

#ifndef MQTT_IO_MUX_H
#define MQTT_IO_MUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <mqtt/mqtt_config.h>
#include <mqtt/mqtt_error.h>
#include "mqtt_platform.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * Event Types
 ******************************************************************************/

/**
 * @brief I/O event flags
 *
 * These flags indicate which events to monitor and which events occurred.
 * Multiple flags can be combined with bitwise OR.
 */
typedef enum mqtt_io_event {
    MQTT_IO_NONE    = 0,       /**< No events */
    MQTT_IO_READ    = (1 << 0), /**< Ready for reading */
    MQTT_IO_WRITE   = (1 << 1), /**< Ready for writing */
    MQTT_IO_ERROR   = (1 << 2), /**< Error condition */
    MQTT_IO_HUP     = (1 << 3)  /**< Hangup (peer closed connection) */
} mqtt_io_event_t;

/*******************************************************************************
 * Multiplexer Types
 ******************************************************************************/

/**
 * @brief I/O multiplexer backend type
 */
typedef enum mqtt_io_mux_type {
    MQTT_IO_MUX_AUTO,    /**< Auto-select best available backend */
    MQTT_IO_MUX_POLL,    /**< POSIX poll() - portable fallback */
    MQTT_IO_MUX_EPOLL,   /**< Linux epoll - high performance */
    MQTT_IO_MUX_KQUEUE   /**< BSD/macOS kqueue - high performance */
} mqtt_io_mux_type_t;

/**
 * @brief I/O event structure
 *
 * Returned by mqtt_io_mux_wait() to indicate which file descriptors
 * have events ready.
 */
typedef struct mqtt_io_ready {
    mqtt_socket_t fd;      /**< File descriptor with events */
    uint32_t events;       /**< Event flags (MQTT_IO_READ, MQTT_IO_WRITE, etc.) */
    void *user_data;       /**< User data associated with this fd */
} mqtt_io_ready_t;

/* Forward declaration of opaque multiplexer handle */
typedef struct mqtt_io_mux mqtt_io_mux_t;

/*******************************************************************************
 * Multiplexer Creation and Destruction
 ******************************************************************************/

/**
 * @brief Create a new I/O multiplexer
 *
 * Creates an I/O multiplexer instance. The backend is auto-selected based
 * on the platform unless explicitly specified.
 *
 * @param type Backend type (MQTT_IO_MUX_AUTO recommended)
 * @param max_events Maximum number of events to handle per wait call
 * @return New multiplexer handle, or NULL on failure
 *
 * @note Call mqtt_io_mux_destroy() to free resources when done.
 */
mqtt_io_mux_t *mqtt_io_mux_create(mqtt_io_mux_type_t type, size_t max_events);

/**
 * @brief Destroy an I/O multiplexer
 *
 * Releases all resources associated with the multiplexer. Any registered
 * file descriptors are automatically removed.
 *
 * @param mux Multiplexer handle (NULL is safe)
 */
void mqtt_io_mux_destroy(mqtt_io_mux_t *mux);

/*******************************************************************************
 * File Descriptor Management
 ******************************************************************************/

/**
 * @brief Add a file descriptor to the multiplexer
 *
 * Registers a file descriptor to be monitored for the specified events.
 *
 * @param mux Multiplexer handle
 * @param fd File descriptor to add
 * @param events Events to monitor (MQTT_IO_READ, MQTT_IO_WRITE, or both)
 * @param user_data User data to associate with this fd (returned in events)
 * @return MQTT_OK on success, error code otherwise
 *
 * @note The fd must not already be registered with this multiplexer.
 */
mqtt_error_t mqtt_io_mux_add(mqtt_io_mux_t *mux, mqtt_socket_t fd,
                              uint32_t events, void *user_data);

/**
 * @brief Modify events for a registered file descriptor
 *
 * Changes which events are monitored for an already-registered fd.
 *
 * @param mux Multiplexer handle
 * @param fd File descriptor to modify
 * @param events New events to monitor
 * @return MQTT_OK on success, error code otherwise
 *
 * @note The fd must already be registered with this multiplexer.
 */
mqtt_error_t mqtt_io_mux_modify(mqtt_io_mux_t *mux, mqtt_socket_t fd,
                                 uint32_t events);

/**
 * @brief Remove a file descriptor from the multiplexer
 *
 * Unregisters a file descriptor. It will no longer be monitored for events.
 *
 * @param mux Multiplexer handle
 * @param fd File descriptor to remove
 * @return MQTT_OK on success, error code otherwise
 */
mqtt_error_t mqtt_io_mux_remove(mqtt_io_mux_t *mux, mqtt_socket_t fd);

/*******************************************************************************
 * Event Waiting
 ******************************************************************************/

/**
 * @brief Wait for I/O events
 *
 * Blocks until one or more registered file descriptors have events ready,
 * or until the timeout expires.
 *
 * @param mux Multiplexer handle
 * @param ready Array to store ready events
 * @param max_ready Maximum number of events to return
 * @param timeout_ms Timeout in milliseconds (-1 = infinite, 0 = poll only)
 * @return Number of ready events (0 on timeout), negative error code on failure
 *
 * @note The ready array is only valid until the next call to mqtt_io_mux_wait().
 */
int mqtt_io_mux_wait(mqtt_io_mux_t *mux, mqtt_io_ready_t *ready,
                      size_t max_ready, int timeout_ms);

/*******************************************************************************
 * Backend Information
 ******************************************************************************/

/**
 * @brief Get the active backend type
 *
 * Returns the actual backend being used by the multiplexer.
 *
 * @param mux Multiplexer handle
 * @return Backend type
 */
mqtt_io_mux_type_t mqtt_io_mux_get_type(mqtt_io_mux_t *mux);

/**
 * @brief Get backend name string
 *
 * Returns a human-readable name for the backend.
 *
 * @param type Backend type
 * @return Static string describing the backend
 */
const char *mqtt_io_mux_type_name(mqtt_io_mux_type_t type);

/**
 * @brief Get the best available backend for this platform
 *
 * Returns the recommended backend type for the current platform.
 *
 * @return Best backend type
 */
mqtt_io_mux_type_t mqtt_io_mux_best_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_IO_MUX_H */
