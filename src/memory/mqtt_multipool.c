/**
 * @file mqtt_multipool.c
 * @brief Multi-Size-Class Memory Pool Allocator Implementation
 */

#include "mqtt_multipool.h"
#include "mqtt_memory.h"
#include <string.h>

/*******************************************************************************
 * Internal Structure
 ******************************************************************************/

/**
 * @brief Internal multi-pool structure
 */
struct mqtt_multipool {
    mqtt_mempool_t pools[MQTT_MULTIPOOL_MAX_CLASSES];  /**< Individual pools */
    size_t num_classes;                                 /**< Number of active size classes */
    size_t sizes[MQTT_MULTIPOOL_MAX_CLASSES];          /**< Sorted size thresholds */
    bool allow_fallback;                                /**< Allow malloc for oversized */

    /* Fallback tracking */
    size_t fallback_allocs;                             /**< Fallback allocation count */
    size_t fallback_bytes;                              /**< Total fallback bytes */
    size_t total_allocs;                                /**< Total allocation calls */
    size_t total_frees;                                 /**< Total free calls */

#ifdef MQTT_THREAD_SAFE
    void *stats_lock;                                   /**< Lock for global stats */
#endif
};

/*******************************************************************************
 * Default Configuration
 ******************************************************************************/

static const size_t default_sizes[MQTT_MULTIPOOL_MAX_CLASSES] =
    MQTT_MULTIPOOL_DEFAULT_SIZES;

void mqtt_multipool_default_config(mqtt_multipool_config_t *config)
{
    if (!config) return;

    memset(config, 0, sizeof(*config));

    config->num_classes = MQTT_MULTIPOOL_MAX_CLASSES;
    config->allow_fallback = true;

    for (size_t i = 0; i < MQTT_MULTIPOOL_MAX_CLASSES; i++) {
        config->classes[i].block_size = default_sizes[i];
        config->classes[i].block_count = MQTT_MULTIPOOL_DEFAULT_BLOCKS;
    }
}

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

/**
 * @brief Find the pool index for a given size
 *
 * Uses binary search to find the smallest size class that can hold the request.
 *
 * @return Pool index, or -1 if size exceeds all classes
 */
static int find_pool_index(mqtt_multipool_t *mp, size_t size)
{
    if (!mp || size == 0) {
        return -1;
    }

    /* Binary search for smallest sufficient size class */
    int low = 0;
    int high = (int)mp->num_classes - 1;
    int result = -1;

    while (low <= high) {
        int mid = (low + high) / 2;

        if (mp->sizes[mid] >= size) {
            result = mid;
            high = mid - 1;  /* Try to find smaller */
        } else {
            low = mid + 1;
        }
    }

    return result;
}

/*******************************************************************************
 * Lock Helpers (Thread Safety)
 ******************************************************************************/

#ifdef MQTT_THREAD_SAFE
#include "../platform/mqtt_platform.h"

static inline void stats_lock(mqtt_multipool_t *mp)
{
    if (mp->stats_lock) {
        mqtt_mutex_lock((mqtt_mutex_t *)mp->stats_lock);
    }
}

static inline void stats_unlock(mqtt_multipool_t *mp)
{
    if (mp->stats_lock) {
        mqtt_mutex_unlock((mqtt_mutex_t *)mp->stats_lock);
    }
}

#else

#define stats_lock(mp)   ((void)0)
#define stats_unlock(mp) ((void)0)

#endif

/*******************************************************************************
 * Creation and Destruction
 ******************************************************************************/

mqtt_multipool_t *mqtt_multipool_create(const mqtt_multipool_config_t *config)
{
    mqtt_multipool_config_t default_cfg;

    /* Use defaults if no config provided */
    if (!config) {
        mqtt_multipool_default_config(&default_cfg);
        config = &default_cfg;
    }

    /* Validate config */
    if (config->num_classes == 0 || config->num_classes > MQTT_MULTIPOOL_MAX_CLASSES) {
        return NULL;
    }

    /* Allocate structure */
    mqtt_multipool_t *mp = mqtt_malloc(sizeof(mqtt_multipool_t));
    if (!mp) {
        return NULL;
    }

    memset(mp, 0, sizeof(*mp));
    mp->allow_fallback = config->allow_fallback;

    /* Sort size classes by block size (create a copy to sort) */
    mqtt_pool_size_class_t sorted_classes[MQTT_MULTIPOOL_MAX_CLASSES];
    memcpy(sorted_classes, config->classes,
           config->num_classes * sizeof(mqtt_pool_size_class_t));

    /* Simple insertion sort (small array, no qsort dependency issues) */
    for (size_t i = 1; i < config->num_classes; i++) {
        mqtt_pool_size_class_t key = sorted_classes[i];
        int j = (int)i - 1;

        while (j >= 0 && sorted_classes[j].block_size > key.block_size) {
            sorted_classes[j + 1] = sorted_classes[j];
            j--;
        }
        sorted_classes[j + 1] = key;
    }

    /* Initialize pools */
    mp->num_classes = 0;

    for (size_t i = 0; i < config->num_classes; i++) {
        /* Skip zero-sized or duplicate classes */
        if (sorted_classes[i].block_size == 0 ||
            sorted_classes[i].block_count == 0) {
            continue;
        }

        /* Skip duplicate sizes */
        if (mp->num_classes > 0 &&
            sorted_classes[i].block_size == mp->sizes[mp->num_classes - 1]) {
            continue;
        }

        mqtt_error_t err = mqtt_mempool_init(
            &mp->pools[mp->num_classes],
            sorted_classes[i].block_size,
            sorted_classes[i].block_count
        );

        if (err != MQTT_OK) {
            /* Cleanup already-initialized pools */
            for (size_t j = 0; j < mp->num_classes; j++) {
                mqtt_mempool_cleanup(&mp->pools[j]);
            }
            mqtt_free(mp);
            return NULL;
        }

        mp->sizes[mp->num_classes] = mp->pools[mp->num_classes].block_size;
        mp->num_classes++;
    }

    if (mp->num_classes == 0) {
        mqtt_free(mp);
        return NULL;
    }

#ifdef MQTT_THREAD_SAFE
    mp->stats_lock = mqtt_malloc(sizeof(mqtt_mutex_t));
    if (mp->stats_lock) {
        if (mqtt_mutex_init((mqtt_mutex_t *)mp->stats_lock) != MQTT_OK) {
            mqtt_free(mp->stats_lock);
            mp->stats_lock = NULL;
        }
    }
#endif

    return mp;
}

void mqtt_multipool_destroy(mqtt_multipool_t *mp)
{
    if (!mp) return;

#ifdef MQTT_THREAD_SAFE
    if (mp->stats_lock) {
        mqtt_mutex_destroy((mqtt_mutex_t *)mp->stats_lock);
        mqtt_free(mp->stats_lock);
    }
#endif

    /* Cleanup all pools */
    for (size_t i = 0; i < mp->num_classes; i++) {
        mqtt_mempool_cleanup(&mp->pools[i]);
    }

    mqtt_free(mp);
}

/*******************************************************************************
 * Allocation Functions
 ******************************************************************************/

void *mqtt_multipool_alloc(mqtt_multipool_t *mp, size_t size)
{
    if (!mp || size == 0) {
        return NULL;
    }

    stats_lock(mp);
    mp->total_allocs++;
    stats_unlock(mp);

    /* Find appropriate pool */
    int idx = find_pool_index(mp, size);

    if (idx >= 0) {
        /* Try to allocate from pool */
        void *ptr = mqtt_mempool_alloc(&mp->pools[idx]);

        if (ptr) {
            return ptr;
        }

        /* Pool exhausted - try larger pools */
        for (int i = idx + 1; i < (int)mp->num_classes; i++) {
            ptr = mqtt_mempool_alloc(&mp->pools[i]);
            if (ptr) {
                return ptr;
            }
        }
    }

    /* Fall back to malloc if allowed */
    if (mp->allow_fallback) {
        void *ptr = mqtt_malloc(size);
        if (ptr) {
            stats_lock(mp);
            mp->fallback_allocs++;
            mp->fallback_bytes += size;
            stats_unlock(mp);
        }
        return ptr;
    }

    return NULL;
}

void *mqtt_multipool_calloc(mqtt_multipool_t *mp, size_t size)
{
    void *ptr = mqtt_multipool_alloc(mp, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void mqtt_multipool_free(mqtt_multipool_t *mp, void *ptr, size_t size)
{
    if (!mp || !ptr || size == 0) {
        return;
    }

    stats_lock(mp);
    mp->total_frees++;
    stats_unlock(mp);

    /* Find the pool that would have handled this size */
    int idx = find_pool_index(mp, size);

    if (idx >= 0) {
        /* Try to free to this pool first */
        mqtt_mempool_t *pool = &mp->pools[idx];

        /* Check if pointer is within this pool's bounds */
        uint8_t *start = (uint8_t *)pool->memory;
        uint8_t *end = start + (pool->block_size * pool->block_count);
        uint8_t *block = (uint8_t *)ptr;

        if (block >= start && block < end) {
            mqtt_mempool_free(pool, ptr);
            return;
        }

        /* Might have come from a larger pool (if original pool was exhausted) */
        for (int i = idx + 1; i < (int)mp->num_classes; i++) {
            pool = &mp->pools[i];
            start = (uint8_t *)pool->memory;
            end = start + (pool->block_size * pool->block_count);

            if (block >= start && block < end) {
                mqtt_mempool_free(pool, ptr);
                return;
            }
        }
    }

    /* Must be a fallback allocation - use regular free */
    mqtt_free(ptr);
}

/*******************************************************************************
 * Statistics Functions
 ******************************************************************************/

mqtt_error_t mqtt_multipool_get_stats(mqtt_multipool_t *mp, mqtt_multipool_stats_t *stats)
{
    if (!mp || !stats) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(stats, 0, sizeof(*stats));

    stats_lock(mp);

    stats->num_classes = mp->num_classes;
    stats->fallback_allocs = mp->fallback_allocs;
    stats->fallback_bytes = mp->fallback_bytes;
    stats->total_allocs = mp->total_allocs;
    stats->total_frees = mp->total_frees;

    for (size_t i = 0; i < mp->num_classes; i++) {
        mqtt_mempool_stats_t pool_stats;
        mqtt_mempool_get_stats(&mp->pools[i], &pool_stats);

        stats->classes[i].block_size = mp->sizes[i];
        stats->classes[i].total_blocks = pool_stats.total_blocks;
        stats->classes[i].used_blocks = pool_stats.used_blocks;
        stats->classes[i].peak_blocks = pool_stats.peak_blocks;
        stats->classes[i].alloc_count = pool_stats.alloc_count;
        stats->classes[i].alloc_failures = pool_stats.alloc_failures;
    }

    stats_unlock(mp);

    return MQTT_OK;
}

void mqtt_multipool_reset_stats(mqtt_multipool_t *mp)
{
    if (!mp) return;

    stats_lock(mp);

    mp->fallback_allocs = 0;
    mp->fallback_bytes = 0;
    mp->total_allocs = 0;
    mp->total_frees = 0;

    for (size_t i = 0; i < mp->num_classes; i++) {
        mqtt_mempool_reset_stats(&mp->pools[i]);
    }

    stats_unlock(mp);
}

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

size_t mqtt_multipool_size_class(mqtt_multipool_t *mp, size_t size)
{
    if (!mp || size == 0) {
        return 0;
    }

    int idx = find_pool_index(mp, size);
    return (idx >= 0) ? mp->sizes[idx] : 0;
}

bool mqtt_multipool_any_exhausted(mqtt_multipool_t *mp)
{
    if (!mp) return false;

    for (size_t i = 0; i < mp->num_classes; i++) {
        if (mqtt_mempool_is_empty(&mp->pools[i])) {
            return true;
        }
    }

    return false;
}

size_t mqtt_multipool_total_memory(mqtt_multipool_t *mp)
{
    if (!mp) return 0;

    size_t total = 0;

    for (size_t i = 0; i < mp->num_classes; i++) {
        total += mp->pools[i].block_size * mp->pools[i].block_count;
    }

    return total;
}
