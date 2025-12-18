/**
 * @file mqtt_multipool.h
 * @brief Multi-Size-Class Memory Pool Allocator
 *
 * Provides a general-purpose pool allocator that manages multiple fixed-size
 * pools with different size classes. Allocations are automatically routed to
 * the appropriate size class based on the requested size.
 *
 * Default size classes: 32, 64, 128, 256, 512, 1024, 2048 bytes
 * Allocations larger than the maximum size class fall back to malloc.
 *
 * Features:
 * - Multiple size classes for reduced fragmentation
 * - O(1) allocation and deallocation within each size class
 * - Automatic size class selection
 * - Fallback to system malloc for oversized allocations
 * - Thread-safe when MQTT_THREAD_SAFE is defined
 * - Unified statistics tracking
 *
 * Usage:
 * @code
 * mqtt_multipool_t *mp = mqtt_multipool_create(NULL);  // Default config
 *
 * void *ptr = mqtt_multipool_alloc(mp, 100);  // Uses 128-byte pool
 * // use ptr...
 * mqtt_multipool_free(mp, ptr, 100);
 *
 * mqtt_multipool_destroy(mp);
 * @endcode
 */

#ifndef MQTT_MULTIPOOL_H
#define MQTT_MULTIPOOL_H

#include <mqtt/mqtt_config.h>
#include <mqtt/mqtt_error.h>
#include "mqtt_pool.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Configuration
 ******************************************************************************/

/** Maximum number of size classes */
#define MQTT_MULTIPOOL_MAX_CLASSES 8

/** Default size classes (in bytes) */
#define MQTT_MULTIPOOL_DEFAULT_SIZES { 32, 64, 128, 256, 512, 1024, 2048, 4096 }

/** Default blocks per size class */
#define MQTT_MULTIPOOL_DEFAULT_BLOCKS 64

/**
 * @brief Size class configuration
 */
typedef struct mqtt_pool_size_class {
    size_t block_size;      /**< Size of blocks in this class */
    size_t block_count;     /**< Number of blocks to pre-allocate */
} mqtt_pool_size_class_t;

/**
 * @brief Multi-pool configuration
 */
typedef struct mqtt_multipool_config {
    mqtt_pool_size_class_t classes[MQTT_MULTIPOOL_MAX_CLASSES]; /**< Size class configs */
    size_t num_classes;     /**< Number of size classes (0 = use defaults) */
    bool allow_fallback;    /**< Allow malloc fallback for oversized allocs */
} mqtt_multipool_config_t;

/*******************************************************************************
 * Statistics
 ******************************************************************************/

/**
 * @brief Per-size-class statistics
 */
typedef struct mqtt_multipool_class_stats {
    size_t block_size;      /**< Block size of this class */
    size_t total_blocks;    /**< Total blocks available */
    size_t used_blocks;     /**< Currently allocated blocks */
    size_t peak_blocks;     /**< Peak usage */
    size_t alloc_count;     /**< Total allocations from this class */
    size_t alloc_failures;  /**< Failed allocations (pool exhausted) */
} mqtt_multipool_class_stats_t;

/**
 * @brief Multi-pool aggregate statistics
 */
typedef struct mqtt_multipool_stats {
    size_t num_classes;                                         /**< Number of size classes */
    mqtt_multipool_class_stats_t classes[MQTT_MULTIPOOL_MAX_CLASSES]; /**< Per-class stats */
    size_t fallback_allocs;  /**< Allocations that fell back to malloc */
    size_t fallback_bytes;   /**< Total bytes allocated via fallback */
    size_t total_allocs;     /**< Total allocation calls */
    size_t total_frees;      /**< Total free calls */
} mqtt_multipool_stats_t;

/*******************************************************************************
 * Types
 ******************************************************************************/

/** Forward declaration */
typedef struct mqtt_multipool mqtt_multipool_t;

/*******************************************************************************
 * Functions
 ******************************************************************************/

/**
 * @brief Create a multi-pool allocator
 *
 * Creates a new multi-pool allocator with the specified configuration.
 * Pass NULL for config to use default size classes and counts.
 *
 * @param config Configuration (NULL for defaults)
 * @return New multi-pool allocator, or NULL on failure
 */
mqtt_multipool_t *mqtt_multipool_create(const mqtt_multipool_config_t *config);

/**
 * @brief Destroy a multi-pool allocator
 *
 * Releases all resources associated with the multi-pool.
 * Any outstanding allocations become invalid.
 *
 * @param mp Multi-pool to destroy (NULL is safe)
 */
void mqtt_multipool_destroy(mqtt_multipool_t *mp);

/**
 * @brief Allocate memory from the multi-pool
 *
 * Allocates at least 'size' bytes from the appropriate size class.
 * The actual allocation may be larger due to size class rounding.
 *
 * @param mp Multi-pool allocator
 * @param size Minimum number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 *
 * @note The returned memory is NOT zero-initialized
 * @note For oversized allocations (if allow_fallback is true), uses malloc
 */
void *mqtt_multipool_alloc(mqtt_multipool_t *mp, size_t size);

/**
 * @brief Allocate and zero-initialize memory
 *
 * Like mqtt_multipool_alloc() but initializes memory to zero.
 *
 * @param mp Multi-pool allocator
 * @param size Minimum number of bytes to allocate
 * @return Pointer to zero-initialized memory, or NULL on failure
 */
void *mqtt_multipool_calloc(mqtt_multipool_t *mp, size_t size);

/**
 * @brief Free memory back to the multi-pool
 *
 * Returns memory to the appropriate pool. The original allocation size
 * must be provided to route the free to the correct pool.
 *
 * @param mp Multi-pool allocator
 * @param ptr Pointer to free (NULL is safe)
 * @param size Original allocation size (must match alloc size)
 *
 * @warning Providing incorrect size results in undefined behavior
 */
void mqtt_multipool_free(mqtt_multipool_t *mp, void *ptr, size_t size);

/**
 * @brief Get multi-pool statistics
 *
 * Copies current statistics to the provided structure.
 *
 * @param mp Multi-pool allocator
 * @param stats Output structure
 * @return MQTT_OK on success, error code otherwise
 */
mqtt_error_t mqtt_multipool_get_stats(mqtt_multipool_t *mp, mqtt_multipool_stats_t *stats);

/**
 * @brief Reset multi-pool statistics
 *
 * Clears allocation counters but does not free memory.
 *
 * @param mp Multi-pool allocator
 */
void mqtt_multipool_reset_stats(mqtt_multipool_t *mp);

/**
 * @brief Find the size class for a given allocation size
 *
 * Returns the block size of the pool that would handle an allocation
 * of the given size. Returns 0 if the size would require fallback.
 *
 * @param mp Multi-pool allocator
 * @param size Allocation size to query
 * @return Block size of handling pool, or 0 for fallback/oversized
 */
size_t mqtt_multipool_size_class(mqtt_multipool_t *mp, size_t size);

/**
 * @brief Check if any pool is exhausted
 *
 * Returns true if any size class pool has no free blocks.
 *
 * @param mp Multi-pool allocator
 * @return true if any pool is exhausted
 */
bool mqtt_multipool_any_exhausted(mqtt_multipool_t *mp);

/**
 * @brief Get total memory used by the multi-pool
 *
 * Returns the total memory allocated for all pools (not including
 * any fallback allocations).
 *
 * @param mp Multi-pool allocator
 * @return Total pool memory in bytes
 */
size_t mqtt_multipool_total_memory(mqtt_multipool_t *mp);

/**
 * @brief Initialize default configuration
 *
 * Populates a configuration structure with default values.
 *
 * @param config Configuration structure to initialize
 */
void mqtt_multipool_default_config(mqtt_multipool_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_MULTIPOOL_H */
