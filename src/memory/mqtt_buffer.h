/**
 * @file mqtt_buffer.h
 * @brief Dynamic Buffer Management
 *
 * Provides a flexible dynamic buffer implementation used for I/O operations,
 * packet serialization, and general data buffering throughout the MQTT library.
 *
 * Buffers can be:
 * - Dynamically allocated and owned (MQTT_BUF_OWNED)
 * - Static/borrowed memory (MQTT_BUF_STATIC)
 * - Read-only (MQTT_BUF_READONLY)
 *
 * The buffer automatically grows as needed when data is appended (if owned).
 */

#ifndef MQTT_BUFFER_H
#define MQTT_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <mqtt/mqtt_error.h>
#include <mqtt/mqtt_config.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Buffer Structure                                                           */
/* ========================================================================== */

/**
 * @brief Dynamic buffer structure
 *
 * A buffer maintains a data pointer, current length, and total capacity.
 * Flags control ownership and mutability semantics.
 */
typedef struct mqtt_buffer {
    uint8_t *data;      /**< Pointer to buffer data */
    size_t len;         /**< Current data length (bytes used) */
    size_t capacity;    /**< Allocated capacity (bytes available) */
    uint32_t flags;     /**< Buffer flags (MQTT_BUF_*) */
} mqtt_buffer_t;

/* ========================================================================== */
/* Buffer Flags                                                               */
/* ========================================================================== */

/**
 * @brief Buffer owns the memory and will free it on cleanup
 *
 * When set, the buffer is responsible for freeing the data pointer.
 * When not set, the buffer uses borrowed/static memory.
 */
#define MQTT_BUF_OWNED      (1U << 0)

/**
 * @brief Buffer is read-only
 *
 * When set, write operations (append, prepend, etc.) will fail.
 * This is useful for wrapping const data.
 */
#define MQTT_BUF_READONLY   (1U << 1)

/**
 * @brief Buffer uses statically allocated memory
 *
 * When set, the buffer uses a fixed-size static buffer.
 * The buffer cannot be resized beyond the initial capacity.
 */
#define MQTT_BUF_STATIC     (1U << 2)

/* ========================================================================== */
/* Buffer Initialization and Cleanup                                          */
/* ========================================================================== */

/**
 * @brief Initialize a dynamically allocated buffer
 *
 * Creates a new buffer with the specified initial capacity. The buffer
 * will automatically grow as needed when data is appended.
 *
 * @param buf Pointer to buffer structure to initialize
 * @param initial_capacity Initial capacity in bytes (0 = use default)
 * @return MQTT_OK on success, error code on failure
 *
 * @note The buffer structure must be zeroed before calling this
 * @note Call mqtt_buffer_cleanup() to release resources
 *
 * @example
 * @code
 * mqtt_buffer_t buf = {0};
 * mqtt_buffer_init(&buf, 1024);
 * // ... use buffer ...
 * mqtt_buffer_cleanup(&buf);
 * @endcode
 */
mqtt_error_t mqtt_buffer_init(mqtt_buffer_t *buf, size_t initial_capacity);

/**
 * @brief Initialize a buffer with static memory
 *
 * Wraps an existing buffer in an mqtt_buffer_t structure. The buffer
 * does not take ownership of the memory and will not free it.
 *
 * @param buf Pointer to buffer structure to initialize
 * @param data Pointer to existing data buffer
 * @param capacity Capacity of the data buffer in bytes
 * @return MQTT_OK on success, error code on failure
 *
 * @note The data pointer must remain valid for the lifetime of the buffer
 * @note The buffer can be written to unless marked readonly
 * @note Call mqtt_buffer_cleanup() to reset the structure (won't free data)
 *
 * @example
 * @code
 * uint8_t static_buf[512];
 * mqtt_buffer_t buf = {0};
 * mqtt_buffer_init_static(&buf, static_buf, sizeof(static_buf));
 * @endcode
 */
mqtt_error_t mqtt_buffer_init_static(mqtt_buffer_t *buf, uint8_t *data, size_t capacity);

/**
 * @brief Cleanup and release buffer resources
 *
 * Frees the buffer's data if it owns the memory. After calling this,
 * the buffer structure is reset to an empty state.
 *
 * @param buf Pointer to buffer to cleanup (NULL is safe)
 *
 * @note Safe to call multiple times
 * @note Does not free the buffer structure itself
 */
void mqtt_buffer_cleanup(mqtt_buffer_t *buf);

/* ========================================================================== */
/* Buffer Capacity Management                                                 */
/* ========================================================================== */

/**
 * @brief Reserve capacity in the buffer
 *
 * Ensures the buffer has at least the specified capacity. If the buffer
 * is smaller, it will be grown. If it's already large enough, no action
 * is taken.
 *
 * @param buf Pointer to buffer
 * @param capacity Minimum required capacity in bytes
 * @return MQTT_OK on success, error code on failure
 *
 * @note Only works on owned buffers (MQTT_BUF_OWNED)
 * @note The actual capacity may be larger than requested
 * @note Does not change the buffer's current length
 */
mqtt_error_t mqtt_buffer_reserve(mqtt_buffer_t *buf, size_t capacity);

/* ========================================================================== */
/* Buffer Write Operations                                                    */
/* ========================================================================== */

/**
 * @brief Append data to the end of the buffer
 *
 * Adds data to the end of the buffer, growing it if necessary.
 * This is the most common write operation.
 *
 * @param buf Pointer to buffer
 * @param data Data to append
 * @param len Length of data in bytes
 * @return MQTT_OK on success, error code on failure
 *
 * @note The buffer will be grown automatically if needed (for owned buffers)
 * @note Returns MQTT_ERR_INVALID_ARG if data is NULL and len > 0
 * @note Returns error if buffer is readonly or static with insufficient space
 */
mqtt_error_t mqtt_buffer_append(mqtt_buffer_t *buf, const void *data, size_t len);

/**
 * @brief Prepend data to the beginning of the buffer
 *
 * Adds data to the beginning of the buffer, shifting existing data.
 * This operation is less efficient than append due to the memmove.
 *
 * @param buf Pointer to buffer
 * @param data Data to prepend
 * @param len Length of data in bytes
 * @return MQTT_OK on success, error code on failure
 *
 * @note The buffer will be grown automatically if needed (for owned buffers)
 * @note Existing data is shifted to make room for new data
 * @note Returns error if buffer is readonly or static with insufficient space
 */
mqtt_error_t mqtt_buffer_prepend(mqtt_buffer_t *buf, const void *data, size_t len);

/* ========================================================================== */
/* Buffer Read Operations                                                     */
/* ========================================================================== */

/**
 * @brief Consume data from the beginning of the buffer
 *
 * Removes the specified number of bytes from the front of the buffer
 * by shifting remaining data forward. This is useful for implementing
 * streaming protocols.
 *
 * @param buf Pointer to buffer
 * @param len Number of bytes to consume
 *
 * @note If len >= buf->len, the buffer becomes empty
 * @note Does not deallocate memory, only adjusts length
 * @note Safe to call with len=0 or on empty buffer
 */
void mqtt_buffer_consume(mqtt_buffer_t *buf, size_t len);

/**
 * @brief Reset the buffer to empty state
 *
 * Clears all data from the buffer but retains the allocated capacity.
 * This is faster than cleanup+init for reusing buffers.
 *
 * @param buf Pointer to buffer
 *
 * @note Does not deallocate memory
 * @note Sets length to 0 but keeps capacity
 */
void mqtt_buffer_reset(mqtt_buffer_t *buf);

/* ========================================================================== */
/* Direct Write Access (Advanced)                                             */
/* ========================================================================== */

/**
 * @brief Get pointer to writable region for direct access
 *
 * Returns a pointer to the unused portion of the buffer where data
 * can be written directly. This is useful for zero-copy operations.
 *
 * @param buf Pointer to buffer
 * @return Pointer to writable region, or NULL if buffer is full or readonly
 *
 * @note Use mqtt_buffer_write_available() to get the size of writable region
 * @note After writing data, call mqtt_buffer_advance_write() to update length
 *
 * @example
 * @code
 * uint8_t *ptr = mqtt_buffer_write_ptr(&buf);
 * size_t available = mqtt_buffer_write_available(&buf);
 * ssize_t n = read(fd, ptr, available);
 * if (n > 0) {
 *     mqtt_buffer_advance_write(&buf, n);
 * }
 * @endcode
 */
uint8_t *mqtt_buffer_write_ptr(mqtt_buffer_t *buf);

/**
 * @brief Get the number of bytes available for direct writing
 *
 * Returns the number of bytes that can be written to the pointer
 * returned by mqtt_buffer_write_ptr().
 *
 * @param buf Pointer to buffer
 * @return Number of bytes available, or 0 if buffer is full or readonly
 */
size_t mqtt_buffer_write_available(mqtt_buffer_t *buf);

/**
 * @brief Advance the write position after direct writing
 *
 * Updates the buffer's length after data has been written directly
 * using mqtt_buffer_write_ptr().
 *
 * @param buf Pointer to buffer
 * @param len Number of bytes that were written
 *
 * @note len must not exceed mqtt_buffer_write_available()
 * @note No bounds checking is performed - caller must ensure validity
 */
void mqtt_buffer_advance_write(mqtt_buffer_t *buf, size_t len);

/* ========================================================================== */
/* Inline Helper Functions                                                    */
/* ========================================================================== */

/**
 * @brief Get the current length of data in the buffer
 *
 * @param buf Pointer to buffer
 * @return Number of bytes of data currently in the buffer
 */
MQTT_INLINE size_t mqtt_buffer_len(const mqtt_buffer_t *buf) {
    return buf ? buf->len : 0;
}

/**
 * @brief Get the total capacity of the buffer
 *
 * @param buf Pointer to buffer
 * @return Total capacity in bytes
 */
MQTT_INLINE size_t mqtt_buffer_capacity(const mqtt_buffer_t *buf) {
    return buf ? buf->capacity : 0;
}

/**
 * @brief Check if the buffer is empty
 *
 * @param buf Pointer to buffer
 * @return true if buffer is empty or NULL, false otherwise
 */
MQTT_INLINE int mqtt_buffer_empty(const mqtt_buffer_t *buf) {
    return !buf || buf->len == 0;
}

/**
 * @brief Get pointer to the buffer's data
 *
 * @param buf Pointer to buffer
 * @return Pointer to buffer data, or NULL if buffer is NULL or empty
 */
MQTT_INLINE uint8_t *mqtt_buffer_data(mqtt_buffer_t *buf) {
    return buf ? buf->data : NULL;
}

/**
 * @brief Get pointer to the buffer's data (const version)
 *
 * @param buf Pointer to buffer
 * @return Pointer to buffer data, or NULL if buffer is NULL or empty
 */
MQTT_INLINE const uint8_t *mqtt_buffer_data_const(const mqtt_buffer_t *buf) {
    return buf ? buf->data : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* MQTT_BUFFER_H */
