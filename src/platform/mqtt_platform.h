/**
 * @file mqtt_platform.h
 * @brief Platform Abstraction Layer for MQTT Client Library
 *
 * This file defines the platform abstraction interface for the MQTT client library.
 * It provides a consistent API for socket operations, time functions, threading primitives,
 * and DNS resolution across different platforms (POSIX, Windows, etc.).
 *
 * Platform implementations must provide all functions declared in this header.
 * The library automatically selects the appropriate implementation based on the
 * build configuration.
 *
 * @note Thread-safe operations are only available when MQTT_THREAD_SAFE is defined.
 */

#ifndef MQTT_PLATFORM_H
#define MQTT_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <mqtt/mqtt_config.h>
#include <mqtt/mqtt_error.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*******************************************************************************
 * Socket Types and Constants
 ******************************************************************************/

#if defined(_WIN32) || defined(_WIN64)
    /* Windows socket type */
    #include <winsock2.h>
    typedef SOCKET mqtt_socket_t;
    #define MQTT_INVALID_SOCKET INVALID_SOCKET
#else
    /* POSIX socket type */
    typedef int mqtt_socket_t;
    #define MQTT_INVALID_SOCKET (-1)
#endif

/* Socket address storage for IPv4/IPv6 */
#include <sys/socket.h>

/*******************************************************************************
 * Socket Functions
 ******************************************************************************/

/**
 * @brief Create a new socket
 *
 * Creates a TCP socket suitable for MQTT communication. The socket is created
 * in blocking mode by default.
 *
 * @param[out] sock Pointer to store the created socket handle
 * @return MQTT_OK on success, error code otherwise
 *
 * @note The caller must close the socket using mqtt_socket_close() when done.
 */
mqtt_error_t mqtt_socket_create(mqtt_socket_t *sock);

/**
 * @brief Connect a socket to a remote host
 *
 * Establishes a TCP connection to the specified host and port. This function
 * implements timeout support by temporarily switching to non-blocking mode
 * if necessary.
 *
 * @param sock Socket handle
 * @param host Hostname or IP address to connect to
 * @param port Port number to connect to
 * @param timeout_ms Connection timeout in milliseconds (0 = blocking)
 * @return MQTT_OK on success, MQTT_ERR_TIMEOUT on timeout, other error codes on failure
 *
 * @note The socket will be restored to its original blocking mode after connection.
 */
mqtt_error_t mqtt_socket_connect(mqtt_socket_t sock, const char *host,
                                  uint16_t port, uint32_t timeout_ms);

/**
 * @brief Close a socket
 *
 * Closes the socket and releases associated resources. The socket handle
 * becomes invalid after this call.
 *
 * @param sock Socket handle to close
 * @return MQTT_OK on success, error code otherwise
 *
 * @note It is safe to call this function multiple times on the same socket.
 */
mqtt_error_t mqtt_socket_close(mqtt_socket_t sock);

/**
 * @brief Send data through a socket
 *
 * Sends data through the socket. This function handles partial sends and
 * interrupted system calls automatically.
 *
 * @param sock Socket handle
 * @param data Pointer to data to send
 * @param len Number of bytes to send
 * @return Number of bytes sent on success, negative error code on failure
 *
 * @note In non-blocking mode, may return MQTT_ERR_WOULD_BLOCK if the operation
 *       would block. The caller should retry later.
 */
ssize_t mqtt_socket_send(mqtt_socket_t sock, const void *data, size_t len);

/**
 * @brief Receive data from a socket
 *
 * Receives data from the socket. This function handles interrupted system calls
 * automatically.
 *
 * @param sock Socket handle
 * @param buf Buffer to store received data
 * @param len Maximum number of bytes to receive
 * @return Number of bytes received on success, 0 on connection close,
 *         negative error code on failure
 *
 * @note In non-blocking mode, may return MQTT_ERR_WOULD_BLOCK if no data is
 *       available. The caller should retry later.
 */
ssize_t mqtt_socket_recv(mqtt_socket_t sock, void *buf, size_t len);

/**
 * @brief Set socket blocking mode
 *
 * Configures whether socket operations should block or return immediately.
 *
 * @param sock Socket handle
 * @param blocking true for blocking mode, false for non-blocking mode
 * @return MQTT_OK on success, error code otherwise
 */
mqtt_error_t mqtt_socket_set_blocking(mqtt_socket_t sock, bool blocking);

/**
 * @brief Set socket timeout values
 *
 * Configures send and receive timeout values for the socket. A timeout of 0
 * means no timeout (infinite wait).
 *
 * @param sock Socket handle
 * @param send_ms Send timeout in milliseconds (0 = no timeout)
 * @param recv_ms Receive timeout in milliseconds (0 = no timeout)
 * @return MQTT_OK on success, error code otherwise
 *
 * @note Not all platforms support fine-grained timeout control. The actual
 *       timeout may be rounded to the nearest supported value.
 */
mqtt_error_t mqtt_socket_set_timeout(mqtt_socket_t sock, uint32_t send_ms,
                                      uint32_t recv_ms);

/**
 * @brief Get the last socket error code
 *
 * Returns the platform-specific error code for the last socket operation
 * that failed. This is useful for detailed error diagnostics.
 *
 * @return Platform-specific error code (errno on POSIX, WSAGetLastError on Windows)
 */
int mqtt_socket_get_error(void);

/*******************************************************************************
 * DNS Resolution
 ******************************************************************************/

/**
 * @brief Resolve hostname to IP address
 *
 * Resolves a hostname or IP address string to a socket address structure
 * suitable for connection. Supports both IPv4 and IPv6 addresses.
 *
 * @param host Hostname or IP address string to resolve
 * @param[out] addr Pointer to socket address storage structure
 * @return MQTT_OK on success, MQTT_ERR_DNS on resolution failure
 *
 * @note The addr structure should be passed to mqtt_socket_connect().
 */
mqtt_error_t mqtt_resolve_host(const char *host, struct sockaddr_storage *addr);

/*******************************************************************************
 * Time Functions
 ******************************************************************************/

/**
 * @brief Get monotonic time in milliseconds
 *
 * Returns a monotonic timestamp in milliseconds. This clock is not affected
 * by system time changes and is suitable for measuring time intervals.
 *
 * @return Monotonic time in milliseconds
 *
 * @note The absolute value is not meaningful. Use only for calculating
 *       time differences between two calls.
 */
uint64_t mqtt_time_monotonic_ms(void);

/**
 * @brief Sleep for specified milliseconds
 *
 * Suspends execution of the calling thread for at least the specified
 * number of milliseconds.
 *
 * @param ms Number of milliseconds to sleep
 *
 * @note On some platforms, the actual sleep time may be longer due to
 *       scheduling granularity.
 */
void mqtt_sleep_ms(uint32_t ms);

/*******************************************************************************
 * Threading Primitives (only if MQTT_THREAD_SAFE is defined)
 ******************************************************************************/

#ifdef MQTT_THREAD_SAFE

#if defined(_WIN32) || defined(_WIN64)
    /* Windows critical section */
    #include <windows.h>
    typedef CRITICAL_SECTION mqtt_mutex_t;
#else
    /* POSIX pthread mutex */
    #include <pthread.h>
    typedef pthread_mutex_t mqtt_mutex_t;
#endif

/**
 * @brief Initialize a mutex
 *
 * Initializes a mutex for use. The mutex must be initialized before
 * any lock/unlock operations.
 *
 * @param[out] mutex Pointer to mutex structure
 * @return MQTT_OK on success, error code otherwise
 *
 * @note The caller must destroy the mutex using mqtt_mutex_destroy() when done.
 */
mqtt_error_t mqtt_mutex_init(mqtt_mutex_t *mutex);

/**
 * @brief Destroy a mutex
 *
 * Destroys a mutex and releases associated resources. The mutex should
 * not be locked when this function is called.
 *
 * @param mutex Pointer to mutex structure
 *
 * @note It is safe to call this function on an uninitialized mutex.
 */
void mqtt_mutex_destroy(mqtt_mutex_t *mutex);

/**
 * @brief Lock a mutex
 *
 * Acquires the mutex lock. If the mutex is already locked by another thread,
 * this function blocks until the mutex becomes available.
 *
 * @param mutex Pointer to mutex structure
 *
 * @note This function never fails. Recursive locking of the same mutex
 *       by the same thread results in undefined behavior.
 */
void mqtt_mutex_lock(mqtt_mutex_t *mutex);

/**
 * @brief Unlock a mutex
 *
 * Releases the mutex lock, allowing other threads to acquire it.
 *
 * @param mutex Pointer to mutex structure
 *
 * @note This function should only be called by the thread that currently
 *       holds the lock. Unlocking a mutex that is not locked results in
 *       undefined behavior.
 */
void mqtt_mutex_unlock(mqtt_mutex_t *mutex);

#endif /* MQTT_THREAD_SAFE */

#ifdef __cplusplus
}
#endif

#endif /* MQTT_PLATFORM_H */
