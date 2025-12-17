/**
 * @file mqtt_memory.h
 * @brief Internal Memory Management Interface
 *
 * This header provides the internal memory allocation interface used throughout
 * the MQTT client library. It supports both default system allocators and custom
 * user-provided allocators.
 *
 * @note This is an internal header - users should use mqtt_set_allocator() from
 *       the public API (mqtt.h) to configure custom allocators.
 */

#ifndef MQTT_MEMORY_H
#define MQTT_MEMORY_H

#include <stddef.h>
#include <mqtt/mqtt_config.h>
#include <mqtt/mqtt.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Default Allocator Macros                                                   */
/* ========================================================================== */

/**
 * @brief Allocate memory
 *
 * Uses the configured custom allocator if set, otherwise falls back to malloc().
 *
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
#define MQTT_MALLOC(size) mqtt_malloc(size)

/**
 * @brief Reallocate memory
 *
 * Uses the configured custom allocator if set, otherwise falls back to realloc().
 *
 * @param ptr Pointer to previously allocated memory (or NULL)
 * @param size New size in bytes
 * @return Pointer to reallocated memory, or NULL on failure
 */
#define MQTT_REALLOC(ptr, size) mqtt_realloc(ptr, size)

/**
 * @brief Free memory
 *
 * Uses the configured custom allocator if set, otherwise falls back to free().
 *
 * @param ptr Pointer to memory to free (NULL is safe)
 */
#define MQTT_FREE(ptr) mqtt_free(ptr)

/**
 * @brief Allocate and zero-initialize memory
 *
 * Uses the configured custom allocator if set, otherwise falls back to calloc().
 *
 * @param count Number of elements
 * @param size Size of each element in bytes
 * @return Pointer to allocated memory, or NULL on failure
 */
#define MQTT_CALLOC(count, size) mqtt_calloc(count, size)

/**
 * @brief Duplicate a string
 *
 * Uses the configured custom allocator if set, otherwise falls back to strdup().
 *
 * @param str String to duplicate
 * @return Pointer to duplicated string, or NULL on failure
 */
#define MQTT_STRDUP(str) mqtt_strdup(str)

/* ========================================================================== */
/* Global Allocator Management                                                */
/* ========================================================================== */

/**
 * @brief Initialize the memory management system
 *
 * This function must be called before any other memory functions are used.
 * It can be called multiple times safely (subsequent calls are ignored).
 *
 * @param alloc Pointer to custom allocator structure (NULL for default allocator)
 *
 * @note This function is thread-safe when compiled with MQTT_THREAD_SAFE
 * @note This is called automatically by mqtt_lib_init()
 */
void mqtt_memory_init(const mqtt_allocator_t *alloc);

/**
 * @brief Cleanup the memory management system
 *
 * This function should be called when shutting down the library to release
 * any internal resources. After calling this, mqtt_memory_init() must be
 * called again before using any memory functions.
 *
 * @note This is called automatically by mqtt_lib_cleanup()
 * @note All allocated memory should be freed before calling this function
 */
void mqtt_memory_cleanup(void);

/**
 * @brief Allocate memory using the configured allocator
 *
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 *
 * @note Use the MQTT_MALLOC() macro instead of calling this directly
 */
void *mqtt_malloc(size_t size);

/**
 * @brief Reallocate memory using the configured allocator
 *
 * @param ptr Pointer to previously allocated memory (or NULL)
 * @param size New size in bytes
 * @return Pointer to reallocated memory, or NULL on failure
 *
 * @note Use the MQTT_REALLOC() macro instead of calling this directly
 * @note If ptr is NULL, this behaves like mqtt_malloc()
 * @note If size is 0, the behavior is implementation-defined
 */
void *mqtt_realloc(void *ptr, size_t size);

/**
 * @brief Free memory using the configured allocator
 *
 * @param ptr Pointer to memory to free (NULL is safe)
 *
 * @note Use the MQTT_FREE() macro instead of calling this directly
 */
void mqtt_free(void *ptr);

/**
 * @brief Allocate and zero-initialize memory using the configured allocator
 *
 * @param count Number of elements
 * @param size Size of each element in bytes
 * @return Pointer to allocated memory, or NULL on failure
 *
 * @note Use the MQTT_CALLOC() macro instead of calling this directly
 */
void *mqtt_calloc(size_t count, size_t size);

/**
 * @brief Duplicate a string using the configured allocator
 *
 * @param str String to duplicate (must be NULL-terminated)
 * @return Pointer to duplicated string, or NULL on failure
 *
 * @note Use the MQTT_STRDUP() macro instead of calling this directly
 * @note The returned string must be freed with mqtt_free()
 */
char *mqtt_strdup(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_MEMORY_H */
