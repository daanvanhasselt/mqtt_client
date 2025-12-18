/**
 * @file mqtt_pool.c
 * @brief Fixed-Size Memory Pool Allocator Implementation
 */

#include "mqtt_pool.h"
#include "mqtt_memory.h"
#include <string.h>
#include <stdint.h>

#ifdef MQTT_THREAD_SAFE
#include "../platform/mqtt_platform.h"
#endif

/*******************************************************************************
 * Helper Macros
 ******************************************************************************/

/* Align size to pointer boundary */
#define ALIGN_SIZE(size) (((size) + sizeof(void*) - 1) & ~(sizeof(void*) - 1))

/* Minimum block size (must hold a pointer for free list) */
#define MIN_BLOCK_SIZE sizeof(void*)

/*******************************************************************************
 * Lock Helpers (Thread Safety)
 ******************************************************************************/

#ifdef MQTT_THREAD_SAFE

static inline void pool_lock(mqtt_mempool_t *pool)
{
    if (pool->lock) {
        mqtt_mutex_lock((mqtt_mutex_t *)pool->lock);
    }
}

static inline void pool_unlock(mqtt_mempool_t *pool)
{
    if (pool->lock) {
        mqtt_mutex_unlock((mqtt_mutex_t *)pool->lock);
    }
}

#else

#define pool_lock(pool)   ((void)0)
#define pool_unlock(pool) ((void)0)

#endif

/*******************************************************************************
 * Pool Implementation
 ******************************************************************************/

mqtt_error_t mqtt_mempool_init(mqtt_mempool_t *pool, size_t block_size, size_t block_count)
{
    if (!pool || block_count == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(pool, 0, sizeof(*pool));

    /* Ensure block size is at least MIN_BLOCK_SIZE and aligned */
    size_t aligned_size = ALIGN_SIZE(block_size);
    if (aligned_size < MIN_BLOCK_SIZE) {
        aligned_size = MIN_BLOCK_SIZE;
    }

    pool->block_size = aligned_size;
    pool->block_count = block_count;

    /* Allocate pool memory */
    size_t total_size = aligned_size * block_count;
    pool->memory = mqtt_malloc(total_size);
    if (!pool->memory) {
        return MQTT_ERR_NOMEM;
    }

    /* Initialize free list - link all blocks together */
    uint8_t *block = (uint8_t *)pool->memory;
    pool->free_list = block;

    for (size_t i = 0; i < block_count - 1; i++) {
        uint8_t *next = block + aligned_size;
        *((void **)block) = next;  /* Store pointer to next block */
        block = next;
    }
    *((void **)block) = NULL;  /* Last block points to NULL */

    /* Initialize statistics */
    pool->stats.total_blocks = block_count;
    pool->stats.used_blocks = 0;
    pool->stats.peak_blocks = 0;
    pool->stats.alloc_count = 0;
    pool->stats.free_count = 0;
    pool->stats.alloc_failures = 0;

#ifdef MQTT_THREAD_SAFE
    /* Initialize mutex */
    pool->lock = mqtt_malloc(sizeof(mqtt_mutex_t));
    if (pool->lock) {
        if (mqtt_mutex_init((mqtt_mutex_t *)pool->lock) != MQTT_OK) {
            mqtt_free(pool->lock);
            pool->lock = NULL;
        }
    }
#endif

    return MQTT_OK;
}

void mqtt_mempool_cleanup(mqtt_mempool_t *pool)
{
    if (!pool) return;

#ifdef MQTT_THREAD_SAFE
    if (pool->lock) {
        mqtt_mutex_destroy((mqtt_mutex_t *)pool->lock);
        mqtt_free(pool->lock);
        pool->lock = NULL;
    }
#endif

    if (pool->memory) {
        mqtt_free(pool->memory);
        pool->memory = NULL;
    }

    pool->free_list = NULL;
    pool->block_size = 0;
    pool->block_count = 0;
}

void *mqtt_mempool_alloc(mqtt_mempool_t *pool)
{
    if (!pool || !pool->memory) {
        return NULL;
    }

    pool_lock(pool);

    void *block = pool->free_list;

    if (block) {
        /* Remove block from free list */
        pool->free_list = *((void **)block);

        /* Update statistics */
        pool->stats.used_blocks++;
        pool->stats.alloc_count++;

        if (pool->stats.used_blocks > pool->stats.peak_blocks) {
            pool->stats.peak_blocks = pool->stats.used_blocks;
        }
    } else {
        /* Pool exhausted */
        pool->stats.alloc_failures++;
    }

    pool_unlock(pool);
    return block;
}

void mqtt_mempool_free(mqtt_mempool_t *pool, void *ptr)
{
    if (!pool || !ptr || !pool->memory) {
        return;
    }

    /* Verify ptr is within pool bounds */
    uint8_t *start = (uint8_t *)pool->memory;
    uint8_t *end = start + (pool->block_size * pool->block_count);
    uint8_t *block = (uint8_t *)ptr;

    if (block < start || block >= end) {
        /* Pointer not from this pool - ignore */
        return;
    }

    /* Verify alignment */
    if ((size_t)(block - start) % pool->block_size != 0) {
        /* Misaligned pointer - ignore */
        return;
    }

    pool_lock(pool);

    /* Add block to front of free list */
    *((void **)block) = pool->free_list;
    pool->free_list = block;

    /* Update statistics */
    pool->stats.used_blocks--;
    pool->stats.free_count++;

    pool_unlock(pool);
}

mqtt_error_t mqtt_mempool_get_stats(mqtt_mempool_t *pool, mqtt_mempool_stats_t *stats)
{
    if (!pool || !stats) {
        return MQTT_ERR_INVALID_ARG;
    }

    pool_lock(pool);
    memcpy(stats, &pool->stats, sizeof(*stats));
    pool_unlock(pool);

    return MQTT_OK;
}

void mqtt_mempool_reset_stats(mqtt_mempool_t *pool)
{
    if (!pool) return;

    pool_lock(pool);

    /* Reset counters but preserve current state */
    pool->stats.peak_blocks = pool->stats.used_blocks;
    pool->stats.alloc_count = 0;
    pool->stats.free_count = 0;
    pool->stats.alloc_failures = 0;

    pool_unlock(pool);
}

bool mqtt_mempool_is_empty(mqtt_mempool_t *pool)
{
    if (!pool) return true;

    pool_lock(pool);
    bool empty = (pool->free_list == NULL);
    pool_unlock(pool);

    return empty;
}

bool mqtt_mempool_is_full(mqtt_mempool_t *pool)
{
    if (!pool) return false;

    pool_lock(pool);
    bool full = (pool->stats.used_blocks == 0);
    pool_unlock(pool);

    return full;
}
