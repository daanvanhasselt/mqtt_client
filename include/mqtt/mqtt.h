/**
 * @file mqtt.h
 * @brief Main Public API Header for MQTT Client Library
 *
 * This is the primary header file that users should include to access the full
 * functionality of the MQTT client library. It provides both synchronous and
 * asynchronous APIs for MQTT 3.1.1 and MQTT 5.0 protocols.
 *
 * Key features:
 * - Support for MQTT 3.1.1 and MQTT 5.0 protocols
 * - Multiple transport layers (TCP, TLS, WebSocket)
 * - Synchronous and asynchronous operation modes
 * - Configurable memory management (custom allocators, pool allocator)
 * - Thread-safe operations (when compiled with MQTT_THREAD_SAFE)
 * - Comprehensive error handling
 * - MQTT 5.0 properties support
 * - Topic matching and validation utilities
 *
 * @example Basic synchronous usage:
 * @code
 * mqtt_lib_init();
 *
 * mqtt_client_config_t config = {
 *     .host = "broker.example.com",
 *     .port = 1883
 * };
 * mqtt_client_t *client = mqtt_client_create(&config);
 *
 * mqtt_connect_opts_t conn_opts = {
 *     .host = "broker.example.com",
 *     .port = 1883,
 *     .client_id = "my_client",
 *     .keepalive_sec = 60,
 *     .clean_session = true,
 *     .protocol_version = MQTT_VERSION_3_1_1
 * };
 *
 * if (mqtt_connect(client, &conn_opts) == MQTT_OK) {
 *     mqtt_publish_opts_t pub_opts = {
 *         .topic = "test/topic",
 *         .payload = (uint8_t*)"Hello MQTT",
 *         .payload_len = 10,
 *         .qos = MQTT_QOS_1
 *     };
 *     mqtt_publish(client, &pub_opts);
 *
 *     mqtt_disconnect(client);
 * }
 *
 * mqtt_client_destroy(client);
 * mqtt_lib_cleanup();
 * @endcode
 */

#ifndef MQTT_H
#define MQTT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>

#include "mqtt_types.h"
#include "mqtt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Memory Management Configuration                                            */
/* ========================================================================== */

/**
 * @brief Custom memory allocator interface
 *
 * Allows users to provide custom memory allocation functions for all
 * library allocations. This is useful for embedded systems, custom
 * memory pools, or debugging/profiling memory usage.
 */
typedef struct mqtt_allocator {
    /**
     * @brief Allocate memory
     * @param size Number of bytes to allocate
     * @param ctx User-provided context pointer
     * @return Pointer to allocated memory, or NULL on failure
     */
    void *(*malloc_fn)(size_t size, void *ctx);

    /**
     * @brief Reallocate memory
     * @param ptr Pointer to previously allocated memory (or NULL)
     * @param size New size in bytes
     * @param ctx User-provided context pointer
     * @return Pointer to reallocated memory, or NULL on failure
     */
    void *(*realloc_fn)(void *ptr, size_t size, void *ctx);

    /**
     * @brief Free memory
     * @param ptr Pointer to memory to free
     * @param ctx User-provided context pointer
     */
    void (*free_fn)(void *ptr, void *ctx);

    /**
     * @brief User-provided context passed to allocator functions
     */
    void *ctx;
} mqtt_allocator_t;

/**
 * @brief Memory pool configuration
 *
 * Configuration for the optional pool allocator, which can improve
 * performance by reducing malloc/free overhead for frequently allocated
 * small objects.
 */
typedef struct mqtt_pool_config {
    size_t small_block_size;       /**< Size of small blocks (default: 64 bytes) */
    size_t small_block_count;      /**< Number of small blocks (default: 32) */

    size_t medium_block_size;      /**< Size of medium blocks (default: 256 bytes) */
    size_t medium_block_count;     /**< Number of medium blocks (default: 16) */

    size_t large_block_size;       /**< Size of large blocks (default: 1024 bytes) */
    size_t large_block_count;      /**< Number of large blocks (default: 8) */

    bool thread_safe;              /**< Enable thread-safe pool operations */
} mqtt_pool_config_t;

/**
 * @brief Client configuration
 *
 * Initial configuration for creating an MQTT client instance.
 * This structure contains settings that cannot be changed after
 * the client is created.
 */
typedef struct mqtt_client_config {
    mqtt_protocol_version_t protocol_version; /**< MQTT protocol version (3.1.1 or 5.0) */
    mqtt_transport_type_t transport_type;     /**< Transport layer type */

    size_t send_buffer_size;       /**< Send buffer size (default: 8KB) */
    size_t recv_buffer_size;       /**< Receive buffer size (default: 8KB) */

    size_t max_inflight_messages;  /**< Maximum in-flight messages (default: 20) */
    uint32_t max_packet_size;      /**< Maximum packet size (default: 256MB) */

    bool use_custom_allocator;     /**< Use custom allocator set via mqtt_set_allocator() */
    bool use_pool_allocator;       /**< Use pool allocator (must be initialized first) */
} mqtt_client_config_t;

/* ========================================================================== */
/* Library Lifecycle                                                          */
/* ========================================================================== */

/**
 * @brief Initialize the MQTT library
 *
 * Must be called once before using any other library functions.
 * This function initializes internal state, platform-specific components,
 * and TLS/WebSocket libraries if enabled.
 *
 * @return MQTT_OK on success, error code on failure
 *
 * @note Thread-safe: Yes (can be called multiple times, will only initialize once)
 * @note This function should be called from the main thread
 *
 * @see mqtt_lib_cleanup
 */
MQTT_API mqtt_error_t mqtt_lib_init(void);

/**
 * @brief Cleanup and shutdown the MQTT library
 *
 * Releases all global resources allocated by the library.
 * Should be called when the application is shutting down.
 * All clients must be destroyed before calling this function.
 *
 * @note Thread-safe: No (must not be called concurrently with other library functions)
 * @note After calling this, mqtt_lib_init() must be called again before using the library
 *
 * @see mqtt_lib_init
 */
MQTT_API void mqtt_lib_cleanup(void);

/**
 * @brief Get library version string
 *
 * Returns the version string of the MQTT library in the format "major.minor.patch".
 *
 * @return Pointer to static version string (e.g., "1.0.0")
 *
 * @note The returned string is always valid and never NULL
 * @note The string should not be modified or freed
 */
MQTT_API const char *mqtt_lib_version(void);

/* ========================================================================== */
/* Memory Configuration                                                       */
/* ========================================================================== */

/**
 * @brief Set custom memory allocator
 *
 * Configures the library to use custom allocation functions for all memory
 * operations. Must be called before mqtt_lib_init() to take effect.
 *
 * @param allocator Pointer to allocator structure with function pointers
 *                  (NULL to restore default malloc/free)
 *
 * @note All allocator function pointers must be non-NULL if allocator is not NULL
 * @note The allocator configuration is global and affects all clients
 * @note Thread-safe: No (must be called before creating any clients)
 *
 * @example
 * @code
 * void *my_malloc(size_t size, void *ctx) {
 *     return custom_allocate(size);
 * }
 *
 * mqtt_allocator_t alloc = {
 *     .malloc_fn = my_malloc,
 *     .realloc_fn = my_realloc,
 *     .free_fn = my_free,
 *     .ctx = NULL
 * };
 * mqtt_set_allocator(&alloc);
 * mqtt_lib_init();
 * @endcode
 */
MQTT_API void mqtt_set_allocator(const mqtt_allocator_t *allocator);

/**
 * @brief Initialize memory pool allocator
 *
 * Initializes an optional memory pool for faster allocation of small objects.
 * The pool must be initialized after mqtt_lib_init() but before creating clients
 * that use the pool allocator.
 *
 * @param config Pool configuration (NULL for default configuration)
 * @return MQTT_OK on success, error code on failure
 *
 * @note Thread-safe: No (must be called before creating clients)
 * @note Only available when compiled with MQTT_ENABLE_POOL_ALLOCATOR
 * @note The pool is global and shared by all clients configured to use it
 *
 * @see mqtt_pool_cleanup
 */
MQTT_API mqtt_error_t mqtt_pool_init(const mqtt_pool_config_t *config);

/**
 * @brief Cleanup memory pool allocator
 *
 * Releases all memory allocated by the pool allocator.
 * All clients using the pool must be destroyed before calling this function.
 *
 * @note Thread-safe: No (must not be called while clients are active)
 * @note After calling this, mqtt_pool_init() must be called again to use the pool
 *
 * @see mqtt_pool_init
 */
MQTT_API void mqtt_pool_cleanup(void);

/* ========================================================================== */
/* Client Lifecycle                                                           */
/* ========================================================================== */

/**
 * @brief Create a new MQTT client instance
 *
 * Allocates and initializes a new MQTT client with the specified configuration.
 * The client is not connected until mqtt_connect() or mqtt_connect_async() is called.
 *
 * @param config Client configuration (NULL for default configuration)
 * @return Pointer to client instance, or NULL on failure
 *
 * @note The returned client must be destroyed with mqtt_client_destroy()
 * @note Use mqtt_client_get_error() to retrieve the error if NULL is returned
 *
 * @see mqtt_client_destroy
 * @see mqtt_client_get_error
 */
MQTT_API mqtt_client_t *mqtt_client_create(const mqtt_client_config_t *config);

/**
 * @brief Destroy an MQTT client instance
 *
 * Disconnects the client (if connected) and releases all resources.
 * After calling this function, the client pointer is invalid and must not be used.
 *
 * @param client Client instance to destroy (NULL is safe)
 *
 * @note If the client is connected, it will be disconnected gracefully
 * @note This function blocks until disconnection is complete
 *
 * @see mqtt_client_create
 */
MQTT_API void mqtt_client_destroy(mqtt_client_t *client);

/**
 * @brief Get the last error that occurred on a client
 *
 * Returns the error code from the most recent operation on the client.
 * Useful for diagnosing why mqtt_client_create() returned NULL or why
 * a callback received an error indication.
 *
 * @param client Client instance
 * @return Error code (MQTT_OK if no error)
 *
 * @note The error code is sticky and persists until cleared by a successful operation
 *
 * @see mqtt_error_str
 */
MQTT_API mqtt_error_t mqtt_client_get_error(mqtt_client_t *client);

/* ========================================================================== */
/* Synchronous API                                                            */
/* ========================================================================== */

/**
 * @brief Connect to MQTT broker (synchronous)
 *
 * Establishes a connection to the MQTT broker and performs the MQTT handshake.
 * This function blocks until the connection is established or an error occurs.
 *
 * @param client Client instance
 * @param opts Connection options
 * @return MQTT_OK on success, error code on failure
 *
 * @note This function blocks for up to connect_timeout_ms (default: 30 seconds)
 * @note The client must not already be connected
 * @note For non-blocking connection, use mqtt_connect_async()
 *
 * @see mqtt_disconnect
 * @see mqtt_connect_async
 */
MQTT_API mqtt_error_t mqtt_connect(mqtt_client_t *client, const mqtt_connect_opts_t *opts);

/**
 * @brief Disconnect from MQTT broker (synchronous)
 *
 * Gracefully disconnects from the broker by sending a DISCONNECT packet.
 * This function blocks until the disconnection is complete.
 *
 * @param client Client instance
 * @return MQTT_OK on success, error code on failure
 *
 * @note The client must be connected
 * @note For MQTT 5.0, you can set properties before disconnecting (future enhancement)
 *
 * @see mqtt_connect
 */
MQTT_API mqtt_error_t mqtt_disconnect(mqtt_client_t *client);

/**
 * @brief Publish a message (synchronous)
 *
 * Publishes a message to the specified topic. For QoS 0, returns immediately.
 * For QoS 1/2, blocks until acknowledgment is received from the broker.
 *
 * @param client Client instance
 * @param opts Publish options (topic, payload, QoS, etc.)
 * @return MQTT_OK on success, error code on failure
 *
 * @note For QoS 0, success only means the message was sent, not delivered
 * @note For QoS 1/2, success means the message was acknowledged
 * @note For non-blocking publish, use mqtt_publish_async()
 *
 * @see mqtt_publish_async
 */
MQTT_API mqtt_error_t mqtt_publish(mqtt_client_t *client, const mqtt_publish_opts_t *opts);

/**
 * @brief Subscribe to topic filters (synchronous)
 *
 * Subscribes to one or more topic filters. This function blocks until
 * the SUBACK response is received from the broker.
 *
 * @param client Client instance
 * @param opts Array of subscription options
 * @param count Number of subscriptions in the array
 * @return MQTT_OK on success, error code on failure
 *
 * @note The broker may grant a different QoS than requested
 * @note For non-blocking subscribe, use mqtt_subscribe_async()
 *
 * @see mqtt_unsubscribe
 * @see mqtt_subscribe_async
 */
MQTT_API mqtt_error_t mqtt_subscribe(mqtt_client_t *client, const mqtt_subscribe_opts_t *opts, size_t count);

/**
 * @brief Unsubscribe from topic filters (synchronous)
 *
 * Unsubscribes from one or more topic filters. This function blocks until
 * the UNSUBACK response is received from the broker.
 *
 * @param client Client instance
 * @param topic_filters Array of topic filter strings
 * @param count Number of topic filters in the array
 * @return MQTT_OK on success, error code on failure
 *
 * @note For non-blocking unsubscribe, use mqtt_unsubscribe_async()
 *
 * @see mqtt_subscribe
 * @see mqtt_unsubscribe_async
 */
MQTT_API mqtt_error_t mqtt_unsubscribe(mqtt_client_t *client, const char **topic_filters, size_t count);

/**
 * @brief Get the number of stored subscriptions
 *
 * Returns the count of subscriptions currently stored for potential restoration.
 * When clean_session=false and the server loses the session (session_present=false),
 * the stored subscriptions can be restored by calling mqtt_restore_subscriptions().
 *
 * @param client Client instance
 * @return Number of stored subscriptions, or 0 if client is NULL or no subscriptions
 *
 * @see mqtt_restore_subscriptions
 */
MQTT_API size_t mqtt_get_stored_subscription_count(mqtt_client_t *client);

/**
 * @brief Restore previously active subscriptions
 *
 * Re-subscribes to all previously active topic filters. This is useful after
 * reconnecting when the server did not preserve the session (session_present=false
 * in CONNACK). Call this from the on_connect callback when session_present is false.
 *
 * @param client Client instance
 * @return MQTT_OK on success, error code on failure
 *
 * @note This function blocks until all subscriptions are confirmed
 * @note Subscriptions are cleared before re-subscribing to avoid duplicates
 * @note If any subscription fails, the remaining subscriptions will still be stored
 *
 * @example
 * @code
 * void on_connect(mqtt_client_t *client, void *user_data, bool session_present) {
 *     if (!session_present && mqtt_get_stored_subscription_count(client) > 0) {
 *         mqtt_restore_subscriptions(client);
 *     }
 * }
 * @endcode
 *
 * @see mqtt_get_stored_subscription_count
 * @see mqtt_subscribe
 */
MQTT_API mqtt_error_t mqtt_restore_subscriptions(mqtt_client_t *client);

/**
 * @brief Run the client event loop (synchronous)
 *
 * Processes network I/O and handles incoming messages and acknowledgments.
 * This function should be called repeatedly in synchronous mode to keep
 * the connection alive and process incoming messages.
 *
 * @param client Client instance
 * @param timeout_ms Timeout in milliseconds (0 for non-blocking, -1 for infinite)
 * @return MQTT_OK on success, error code on failure
 *
 * @note In synchronous mode, call this regularly (at least once per keepalive interval)
 * @note This function processes one batch of I/O and then returns
 * @note Callbacks (on_message, etc.) will be invoked from this function
 *
 * @see mqtt_loop_tick
 */
MQTT_API mqtt_error_t mqtt_loop(mqtt_client_t *client, int timeout_ms);

/**
 * @brief Process one iteration of the event loop (synchronous)
 *
 * Similar to mqtt_loop() but processes exactly one iteration without timeout.
 * This is useful for integrating with external event loops.
 *
 * @param client Client instance
 * @return MQTT_OK on success, error code on failure
 *
 * @note This is equivalent to mqtt_loop(client, 0)
 * @note Returns immediately if no I/O is ready
 *
 * @see mqtt_loop
 */
MQTT_API mqtt_error_t mqtt_loop_tick(mqtt_client_t *client);

/* ========================================================================== */
/* Asynchronous API                                                           */
/* ========================================================================== */

/**
 * @brief Set callback functions for asynchronous events
 *
 * Registers callback functions that will be invoked when events occur
 * (connection established, message received, etc.). This must be called
 * before using the asynchronous API.
 *
 * @param client Client instance
 * @param callbacks Pointer to callbacks structure
 *
 * @note Callbacks are invoked from the thread that calls mqtt_process_read/write
 * @note Set callback pointers to NULL to disable specific callbacks
 * @note The callbacks pointer must remain valid for the lifetime of the client
 *
 * @see mqtt_callbacks_t
 */
MQTT_API void mqtt_set_callbacks(mqtt_client_t *client, const mqtt_callbacks_t *callbacks);

/**
 * @brief Connect to MQTT broker (asynchronous)
 *
 * Initiates a connection to the MQTT broker. This function returns immediately
 * without blocking. The connection process continues in the background.
 * Call mqtt_process_read() and mqtt_process_write() to make progress.
 *
 * @param client Client instance
 * @param opts Connection options
 * @return MQTT_OK if initiated successfully, error code on immediate failure
 *
 * @note The on_connect callback is invoked when connection completes
 * @note The on_disconnect callback is invoked if connection fails
 * @note Must call mqtt_process_read/write to complete the connection
 *
 * @see mqtt_connect
 * @see mqtt_process_read
 * @see mqtt_process_write
 */
MQTT_API mqtt_error_t mqtt_connect_async(mqtt_client_t *client, const mqtt_connect_opts_t *opts);

/**
 * @brief Publish a message (asynchronous)
 *
 * Initiates a publish operation. This function returns immediately without blocking.
 * For QoS 1/2, the packet ID is returned in packet_id_out for tracking.
 *
 * @param client Client instance
 * @param opts Publish options
 * @param packet_id_out Pointer to receive packet ID (NULL if not needed, only valid for QoS > 0)
 * @return MQTT_OK if initiated successfully, error code on immediate failure
 *
 * @note For QoS 0, on_publish_complete is not called
 * @note For QoS 1/2, on_publish_complete is called when acknowledged
 * @note Must call mqtt_process_read/write to complete the publish
 *
 * @see mqtt_publish
 * @see mqtt_on_publish_complete_cb
 */
MQTT_API mqtt_error_t mqtt_publish_async(mqtt_client_t *client, const mqtt_publish_opts_t *opts, uint16_t *packet_id_out);

/**
 * @brief Subscribe to topic filters (asynchronous)
 *
 * Initiates a subscribe operation. This function returns immediately without blocking.
 * The packet ID is returned in packet_id_out for tracking.
 *
 * @param client Client instance
 * @param opts Array of subscription options
 * @param count Number of subscriptions in the array
 * @param packet_id_out Pointer to receive packet ID (NULL if not needed)
 * @return MQTT_OK if initiated successfully, error code on immediate failure
 *
 * @note The on_subscribe callback is called when the SUBACK is received
 * @note Must call mqtt_process_read/write to complete the subscribe
 *
 * @see mqtt_subscribe
 * @see mqtt_on_subscribe_cb
 */
MQTT_API mqtt_error_t mqtt_subscribe_async(mqtt_client_t *client, const mqtt_subscribe_opts_t *opts,
                                           size_t count, uint16_t *packet_id_out);

/**
 * @brief Unsubscribe from topic filters (asynchronous)
 *
 * Initiates an unsubscribe operation. This function returns immediately without blocking.
 * The packet ID is returned in packet_id_out for tracking.
 *
 * @param client Client instance
 * @param topic_filters Array of topic filter strings
 * @param count Number of topic filters in the array
 * @param packet_id_out Pointer to receive packet ID (NULL if not needed)
 * @return MQTT_OK if initiated successfully, error code on immediate failure
 *
 * @note Must call mqtt_process_read/write to complete the unsubscribe
 *
 * @see mqtt_unsubscribe
 */
MQTT_API mqtt_error_t mqtt_unsubscribe_async(mqtt_client_t *client, const char **topic_filters,
                                             size_t count, uint16_t *packet_id_out);

/**
 * @brief Get the underlying socket file descriptor
 *
 * Returns the socket file descriptor for integration with external event loops
 * (select, poll, epoll, kqueue, etc.). The socket can be monitored for
 * read/write readiness.
 *
 * @param client Client instance
 * @return Socket file descriptor, or -1 if not connected or on error
 *
 * @note The socket is non-blocking in asynchronous mode
 * @note Do not read/write directly to the socket; use mqtt_process_read/write
 *
 * @see mqtt_want_write
 * @see mqtt_process_read
 * @see mqtt_process_write
 */
MQTT_API int mqtt_get_socket_fd(mqtt_client_t *client);

/**
 * @brief Check if the client has data to write
 *
 * Returns true if the client has data in the send buffer that needs to be
 * written to the socket. Use this to determine if you should monitor the
 * socket for write readiness in your event loop.
 *
 * @param client Client instance
 * @return true if write is needed, false otherwise
 *
 * @note Call mqtt_process_write() when the socket becomes writable
 *
 * @see mqtt_get_socket_fd
 * @see mqtt_process_write
 */
MQTT_API bool mqtt_want_write(mqtt_client_t *client);

/**
 * @brief Process incoming data from the socket
 *
 * Reads available data from the socket and processes incoming MQTT packets.
 * This function should be called when the socket is readable (as indicated
 * by select/poll/etc.).
 *
 * @param client Client instance
 * @return MQTT_OK on success, error code on failure
 *
 * @note This function does not block
 * @note Callbacks may be invoked from this function
 * @note Returns MQTT_ERR_WOULD_BLOCK if no data is available (not an error)
 *
 * @see mqtt_get_socket_fd
 * @see mqtt_process_write
 */
MQTT_API mqtt_error_t mqtt_process_read(mqtt_client_t *client);

/**
 * @brief Process outgoing data to the socket
 *
 * Writes pending data from the send buffer to the socket. This function
 * should be called when the socket is writable (as indicated by select/poll/etc.)
 * and mqtt_want_write() returns true.
 *
 * @param client Client instance
 * @return MQTT_OK on success, error code on failure
 *
 * @note This function does not block
 * @note Returns MQTT_ERR_WOULD_BLOCK if the socket is not ready (not an error)
 *
 * @see mqtt_get_socket_fd
 * @see mqtt_want_write
 * @see mqtt_process_read
 */
MQTT_API mqtt_error_t mqtt_process_write(mqtt_client_t *client);

/* ========================================================================== */
/* MQTT 5.0 Specific Functions                                                */
/* ========================================================================== */

/**
 * @brief Send AUTH packet (MQTT 5.0 enhanced authentication)
 *
 * Sends an AUTH packet for enhanced authentication flow. This is only
 * available when using MQTT 5.0.
 *
 * @param client Client instance
 * @param reason_code Authentication reason code
 * @param props Authentication properties (can be NULL)
 * @return MQTT_OK on success, error code on failure
 *
 * @note Only available for MQTT 5.0 clients
 * @note This is typically used with SASL or other authentication mechanisms
 *
 * @see mqtt_connect
 */
MQTT_API mqtt_error_t mqtt_auth(mqtt_client_t *client, uint8_t reason_code, mqtt_property_t *props);

/**
 * @brief Get the assigned client ID (MQTT 5.0)
 *
 * Returns the client ID assigned by the broker. This is useful when
 * connecting with an empty client ID and the broker assigns one.
 *
 * @param client Client instance
 * @return Pointer to client ID string, or NULL if not connected or not assigned
 *
 * @note Only relevant for MQTT 5.0 or when the broker assigns a client ID
 * @note The returned string is valid until the client is destroyed or reconnected
 */
MQTT_API const char *mqtt_get_assigned_client_id(mqtt_client_t *client);

/**
 * @brief Create a new MQTT 5.0 property
 *
 * Allocates and initializes a new property structure. The property
 * type and initial value must be set using the appropriate setter functions.
 *
 * @param id Property identifier (from MQTT 5.0 specification)
 * @return Pointer to new property, or NULL on allocation failure
 *
 * @note The property must be freed with mqtt_property_free() or mqtt_property_free_all()
 * @note Only available when compiled with MQTT_ENABLE_V5
 *
 * @see mqtt_property_free
 * @see mqtt_property_free_all
 */
MQTT_API mqtt_property_t *mqtt_property_create(uint8_t id);

/**
 * @brief Free a single property
 *
 * Releases memory for a single property. If the property is part of a list,
 * it is removed from the list first.
 *
 * @param prop Property to free (NULL is safe)
 *
 * @see mqtt_property_create
 * @see mqtt_property_free_all
 */
MQTT_API void mqtt_property_free(mqtt_property_t *prop);

/**
 * @brief Free all properties in a linked list
 *
 * Releases memory for all properties in the linked list starting at the
 * given property.
 *
 * @param props First property in the list (NULL is safe)
 *
 * @see mqtt_property_create
 * @see mqtt_property_free
 */
MQTT_API void mqtt_property_free_all(mqtt_property_t *props);

/**
 * @brief Set a byte property value
 *
 * @param prop Property to modify
 * @param value Byte value (0-255)
 * @return MQTT_OK on success, error code on failure
 */
MQTT_API mqtt_error_t mqtt_property_set_byte(mqtt_property_t *prop, uint8_t value);

/**
 * @brief Set a 16-bit integer property value
 *
 * @param prop Property to modify
 * @param value 16-bit unsigned integer value
 * @return MQTT_OK on success, error code on failure
 */
MQTT_API mqtt_error_t mqtt_property_set_u16(mqtt_property_t *prop, uint16_t value);

/**
 * @brief Set a 32-bit integer property value
 *
 * @param prop Property to modify
 * @param value 32-bit unsigned integer value
 * @return MQTT_OK on success, error code on failure
 */
MQTT_API mqtt_error_t mqtt_property_set_u32(mqtt_property_t *prop, uint32_t value);

/**
 * @brief Set a string property value
 *
 * @param prop Property to modify
 * @param value UTF-8 encoded string (will be copied)
 * @return MQTT_OK on success, error code on failure
 *
 * @note The string is copied internally; the caller retains ownership of the input
 */
MQTT_API mqtt_error_t mqtt_property_set_string(mqtt_property_t *prop, const char *value);

/**
 * @brief Set a string pair property value (key-value)
 *
 * Used for user properties and other key-value pairs in MQTT 5.0.
 *
 * @param prop Property to modify
 * @param key UTF-8 encoded key string (will be copied)
 * @param value UTF-8 encoded value string (will be copied)
 * @return MQTT_OK on success, error code on failure
 *
 * @note Both strings are copied internally
 */
MQTT_API mqtt_error_t mqtt_property_set_string_pair(mqtt_property_t *prop, const char *key, const char *value);

/**
 * @brief Set a binary data property value
 *
 * @param prop Property to modify
 * @param data Binary data (will be copied)
 * @param len Length of binary data in bytes
 * @return MQTT_OK on success, error code on failure
 *
 * @note The data is copied internally; the caller retains ownership of the input
 */
MQTT_API mqtt_error_t mqtt_property_set_binary(mqtt_property_t *prop, const uint8_t *data, size_t len);

/**
 * @brief Append a property to a property list
 *
 * Adds a property to the end of a property linked list. This is used to
 * build property chains for CONNECT, PUBLISH, SUBSCRIBE, etc.
 *
 * @param list Pointer to list head (updated to point to new head if *list is NULL)
 * @param prop Property to append (must not already be in a list)
 * @return MQTT_OK on success, error code on failure
 *
 * @note The list takes ownership of the property
 * @note Use mqtt_property_free_all() to free the entire list
 */
MQTT_API mqtt_error_t mqtt_property_append(mqtt_property_t **list, mqtt_property_t *prop);

/* ========================================================================== */
/* Utility Functions                                                          */
/* ========================================================================== */

/**
 * @brief Check if a topic matches a topic filter
 *
 * Tests whether a topic name matches a topic filter pattern, taking into
 * account MQTT wildcard rules (+ for single level, # for multi-level).
 *
 * @param filter Topic filter (may contain wildcards)
 * @param topic Topic name (must not contain wildcards)
 * @return true if topic matches filter, false otherwise
 *
 * @example
 * @code
 * mqtt_topic_matches("sensors/+/temperature", "sensors/bedroom/temperature"); // true
 * mqtt_topic_matches("sensors/#", "sensors/bedroom/temperature");             // true
 * mqtt_topic_matches("sensors/+", "sensors/bedroom/temperature");             // false
 * @endcode
 */
MQTT_API bool mqtt_topic_matches(const char *filter, const char *topic);

/**
 * @brief Validate a topic name or topic filter
 *
 * Checks if a topic string is valid according to MQTT specification rules.
 *
 * @param topic Topic string to validate
 * @param is_filter true if topic is a filter (wildcards allowed), false for topic name
 * @return true if valid, false otherwise
 *
 * @note Topic names cannot contain wildcards
 * @note Topic filters can contain + and # wildcards
 * @note Topics cannot exceed MQTT_MAX_TOPIC_LENGTH
 */
MQTT_API bool mqtt_topic_valid(const char *topic, bool is_filter);

/**
 * @brief Logging callback function type
 *
 * @param level Log level (0=ERROR, 1=WARN, 2=INFO, 3=DEBUG, 4=TRACE)
 * @param file Source file name
 * @param line Source line number
 * @param fmt Format string (printf-style)
 * @param args Variable arguments list
 */
typedef void (*mqtt_log_cb)(int level, const char *file, int line, const char *fmt, va_list args);

/**
 * @brief Set custom logging callback
 *
 * Registers a custom logging function to receive all log messages from the library.
 * By default, logs go to stderr.
 *
 * @param callback Logging callback function (NULL to disable logging)
 *
 * @note Only available when compiled with MQTT_ENABLE_LOGGING
 * @note The callback must be thread-safe if using multiple clients
 *
 * @see mqtt_set_log_level
 */
MQTT_API void mqtt_set_log_callback(mqtt_log_cb callback);

/**
 * @brief Set the logging level
 *
 * Controls the verbosity of log output. Messages below this level are not logged.
 *
 * @param level Log level (0=ERROR, 1=WARN, 2=INFO, 3=DEBUG, 4=TRACE)
 *
 * @note Only available when compiled with MQTT_ENABLE_LOGGING
 * @note Default level is INFO (2)
 *
 * @see mqtt_set_log_callback
 */
MQTT_API void mqtt_set_log_level(int level);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_H */
