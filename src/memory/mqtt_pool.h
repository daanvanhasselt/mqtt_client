/**
 * @file mqtt_pool.h
 * @brief Fixed-Size Memory Pool Allocator (Instance-based)
 *
 * Provides an instance-based memory pool allocator for fixed-size allocations.
 * This is useful for high-performance scenarios where many small, same-sized
 * objects are allocated and freed frequently (e.g., MQTT packets, inflight entries).
 *
 * This is distinct from the global pool API in mqtt.h - this allows multiple
 * independent pools with different configurations.
 *
 * Features:
 * - O(1) allocation and deallocation
 * - No fragmentation within the pool
 * - Thread-safe when MQTT_THREAD_SAFE is defined
 * - Statistics tracking for debugging
 *
 * Usage:
 * @code
 * mqtt_mempool_t pool;
 * mqtt_mempool_init(&pool, sizeof(my_struct_t), 100);
 *
 * my_struct_t *obj = mqtt_mempool_alloc(&pool);
 * // use obj...
 * mqtt_mempool_free(&pool, obj);
 *
 * mqtt_mempool_cleanup(&pool);
 * @endcode
 */

#ifndef MQTT_MEMPOOL_H
#define MQTT_MEMPOOL_H

#include <mqtt/mqtt_config.h>
#include <mqtt/mqtt_error.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Pool Statistics
 ******************************************************************************/

/**
 * @brief Memory pool statistics
 */
typedef struct mqtt_mempool_stats {
    size_t total_blocks;      /**< Total number of blocks in pool */
    size_t used_blocks;       /**< Currently allocated blocks */
    size_t peak_blocks;       /**< Peak number of allocated blocks */
    size_t alloc_count;       /**< Total allocation calls */
    size_t free_count;        /**< Total free calls */
    size_t alloc_failures;    /**< Failed allocation attempts */
} mqtt_mempool_stats_t;

/*******************************************************************************
 * Pool Structure
 ******************************************************************************/

/**
 * @brief Memory pool structure
 *
 * A memory pool pre-allocates a contiguous block of memory and divides it
 * into fixed-size blocks. Free blocks are maintained in a linked list for
 * O(1) allocation and deallocation.
 */
typedef struct mqtt_mempool {
    void *memory;                 /**< Pool memory buffer */
    void *free_list;              /**< Head of free block list */
    size_t block_size;            /**< Size of each block (aligned) */
    size_t block_count;           /**< Total number of blocks */
    mqtt_mempool_stats_t stats;   /**< Usage statistics */

#ifdef MQTT_THREAD_SAFE
    void *lock;                   /**< Thread synchronization (opaque) */
#endif
} mqtt_mempool_t;

/*******************************************************************************
 * Pool Functions
 ******************************************************************************/

/**
 * @brief Initialize a memory pool
 *
 * Creates a pool with the specified number of fixed-size blocks.
 * The block size is automatically aligned to pointer size.
 *
 * @param pool Pool structure to initialize
 * @param block_size Size of each block in bytes
 * @param block_count Number of blocks to allocate
 * @return MQTT_OK on success, error code otherwise
 *
 * @note The actual block size may be larger than requested due to alignment
 * @note Call mqtt_mempool_cleanup() to release resources
 */
mqtt_error_t mqtt_mempool_init(mqtt_mempool_t *pool, size_t block_size, size_t block_count);

/**
 * @brief Cleanup and release pool resources
 *
 * Frees all memory associated with the pool. Any pointers previously
 * allocated from the pool become invalid.
 *
 * @param pool Pool to cleanup (NULL is safe)
 *
 * @warning All allocated blocks should be freed before calling this
 */
void mqtt_mempool_cleanup(mqtt_mempool_t *pool);

/**
 * @brief Allocate a block from the pool
 *
 * Returns a pointer to a free block from the pool. The block is
 * NOT initialized (contains garbage data).
 *
 * @param pool Pool to allocate from
 * @return Pointer to allocated block, or NULL if pool is exhausted
 *
 * @note O(1) operation
 * @note Thread-safe when MQTT_THREAD_SAFE is defined
 */
void *mqtt_mempool_alloc(mqtt_mempool_t *pool);

/**
 * @brief Return a block to the pool
 *
 * Marks a previously allocated block as free and returns it to the pool.
 *
 * @param pool Pool the block was allocated from
 * @param ptr Block to free (NULL is safe)
 *
 * @note O(1) operation
 * @note Thread-safe when MQTT_THREAD_SAFE is defined
 * @warning Freeing a block not from this pool results in undefined behavior
 */
void mqtt_mempool_free(mqtt_mempool_t *pool, void *ptr);

/**
 * @brief Get pool statistics
 *
 * Copies current pool statistics to the provided structure.
 *
 * @param pool Pool to get statistics from
 * @param stats Output structure for statistics
 * @return MQTT_OK on success, error code otherwise
 */
mqtt_error_t mqtt_mempool_get_stats(mqtt_mempool_t *pool, mqtt_mempool_stats_t *stats);

/**
 * @brief Reset pool statistics
 *
 * Clears the allocation counters (alloc_count, free_count, etc.)
 * but does not free any allocated blocks.
 *
 * @param pool Pool to reset statistics for
 */
void mqtt_mempool_reset_stats(mqtt_mempool_t *pool);

/**
 * @brief Check if pool is empty (all blocks allocated)
 *
 * @param pool Pool to check
 * @return true if no free blocks available, false otherwise
 */
bool mqtt_mempool_is_empty(mqtt_mempool_t *pool);

/**
 * @brief Check if pool is full (no blocks allocated)
 *
 * @param pool Pool to check
 * @return true if all blocks are free, false otherwise
 */
bool mqtt_mempool_is_full(mqtt_mempool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_POOL_H */
