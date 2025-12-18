/**
 * @file test_pool.c
 * @brief Unit tests for memory pool allocator
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/memory/mqtt_pool.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  Testing %s... ", #name); \
    if (test_##name()) { \
        printf("PASSED\n"); \
        tests_passed++; \
    } else { \
        printf("FAILED\n"); \
    } \
    tests_run++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\n    ASSERT failed: %s (line %d)\n", #cond, __LINE__); \
        return 0; \
    } \
} while(0)

/*******************************************************************************
 * Test Cases
 ******************************************************************************/

static int test_init_cleanup(void)
{
    mqtt_mempool_t pool;

    mqtt_error_t err = mqtt_mempool_init(&pool, 64, 10);
    ASSERT(err == MQTT_OK);
    ASSERT(pool.memory != NULL);
    ASSERT(pool.block_size >= 64);
    ASSERT(pool.block_count == 10);

    mqtt_mempool_cleanup(&pool);
    ASSERT(pool.memory == NULL);

    return 1;
}

static int test_alloc_free(void)
{
    mqtt_mempool_t pool;
    ASSERT(mqtt_mempool_init(&pool, 32, 5) == MQTT_OK);

    /* Allocate a block */
    void *ptr = mqtt_mempool_alloc(&pool);
    ASSERT(ptr != NULL);

    /* Verify statistics */
    mqtt_mempool_stats_t stats;
    mqtt_mempool_get_stats(&pool, &stats);
    ASSERT(stats.used_blocks == 1);
    ASSERT(stats.alloc_count == 1);

    /* Free the block */
    mqtt_mempool_free(&pool, ptr);

    mqtt_mempool_get_stats(&pool, &stats);
    ASSERT(stats.used_blocks == 0);
    ASSERT(stats.free_count == 1);

    mqtt_mempool_cleanup(&pool);
    return 1;
}

static int test_exhaust_pool(void)
{
    mqtt_mempool_t pool;
    ASSERT(mqtt_mempool_init(&pool, 16, 3) == MQTT_OK);

    /* Allocate all blocks */
    void *p1 = mqtt_mempool_alloc(&pool);
    void *p2 = mqtt_mempool_alloc(&pool);
    void *p3 = mqtt_mempool_alloc(&pool);

    ASSERT(p1 != NULL);
    ASSERT(p2 != NULL);
    ASSERT(p3 != NULL);

    /* Pool should be empty now */
    ASSERT(mqtt_mempool_is_empty(&pool) == true);

    /* Next allocation should fail */
    void *p4 = mqtt_mempool_alloc(&pool);
    ASSERT(p4 == NULL);

    /* Check failure was recorded */
    mqtt_mempool_stats_t stats;
    mqtt_mempool_get_stats(&pool, &stats);
    ASSERT(stats.alloc_failures == 1);

    /* Free one and allocate again */
    mqtt_mempool_free(&pool, p2);
    ASSERT(mqtt_mempool_is_empty(&pool) == false);

    void *p5 = mqtt_mempool_alloc(&pool);
    ASSERT(p5 != NULL);
    ASSERT(p5 == p2);  /* Should get same block back */

    mqtt_mempool_free(&pool, p1);
    mqtt_mempool_free(&pool, p3);
    mqtt_mempool_free(&pool, p5);

    mqtt_mempool_cleanup(&pool);
    return 1;
}

static int test_peak_tracking(void)
{
    mqtt_mempool_t pool;
    ASSERT(mqtt_mempool_init(&pool, 16, 10) == MQTT_OK);

    void *ptrs[5];

    /* Allocate 5 blocks */
    for (int i = 0; i < 5; i++) {
        ptrs[i] = mqtt_mempool_alloc(&pool);
        ASSERT(ptrs[i] != NULL);
    }

    mqtt_mempool_stats_t stats;
    mqtt_mempool_get_stats(&pool, &stats);
    ASSERT(stats.peak_blocks == 5);

    /* Free 3 blocks */
    for (int i = 0; i < 3; i++) {
        mqtt_mempool_free(&pool, ptrs[i]);
    }

    mqtt_mempool_get_stats(&pool, &stats);
    ASSERT(stats.used_blocks == 2);
    ASSERT(stats.peak_blocks == 5);  /* Peak unchanged */

    /* Allocate 4 more to beat peak */
    for (int i = 0; i < 4; i++) {
        ptrs[i] = mqtt_mempool_alloc(&pool);
        ASSERT(ptrs[i] != NULL);
    }

    mqtt_mempool_get_stats(&pool, &stats);
    ASSERT(stats.used_blocks == 6);
    ASSERT(stats.peak_blocks == 6);  /* New peak */

    /* Cleanup */
    mqtt_mempool_free(&pool, ptrs[0]);
    mqtt_mempool_free(&pool, ptrs[1]);
    mqtt_mempool_free(&pool, ptrs[2]);
    mqtt_mempool_free(&pool, ptrs[3]);
    mqtt_mempool_free(&pool, ptrs[4]);

    mqtt_mempool_cleanup(&pool);
    return 1;
}

static int test_is_full(void)
{
    mqtt_mempool_t pool;
    ASSERT(mqtt_mempool_init(&pool, 16, 3) == MQTT_OK);

    /* Initially full (no blocks allocated) */
    ASSERT(mqtt_mempool_is_full(&pool) == true);

    void *p = mqtt_mempool_alloc(&pool);
    ASSERT(mqtt_mempool_is_full(&pool) == false);

    mqtt_mempool_free(&pool, p);
    ASSERT(mqtt_mempool_is_full(&pool) == true);

    mqtt_mempool_cleanup(&pool);
    return 1;
}

static int test_alignment(void)
{
    mqtt_mempool_t pool;

    /* Request very small block size */
    ASSERT(mqtt_mempool_init(&pool, 1, 5) == MQTT_OK);

    /* Block size should be at least pointer-sized for free list */
    ASSERT(pool.block_size >= sizeof(void*));

    /* Allocations should be aligned */
    void *p1 = mqtt_mempool_alloc(&pool);
    void *p2 = mqtt_mempool_alloc(&pool);

    ASSERT(p1 != NULL);
    ASSERT(p2 != NULL);
    ASSERT(((uintptr_t)p1 % sizeof(void*)) == 0);
    ASSERT(((uintptr_t)p2 % sizeof(void*)) == 0);

    mqtt_mempool_free(&pool, p1);
    mqtt_mempool_free(&pool, p2);
    mqtt_mempool_cleanup(&pool);

    return 1;
}

static int test_invalid_free(void)
{
    mqtt_mempool_t pool;
    ASSERT(mqtt_mempool_init(&pool, 32, 5) == MQTT_OK);

    /* Free NULL - should be safe */
    mqtt_mempool_free(&pool, NULL);

    /* Free pointer not from pool - should be ignored */
    int local_var;
    mqtt_mempool_free(&pool, &local_var);

    /* Statistics should be unchanged */
    mqtt_mempool_stats_t stats;
    mqtt_mempool_get_stats(&pool, &stats);
    ASSERT(stats.free_count == 0);

    mqtt_mempool_cleanup(&pool);
    return 1;
}

static int test_reset_stats(void)
{
    mqtt_mempool_t pool;
    ASSERT(mqtt_mempool_init(&pool, 16, 5) == MQTT_OK);

    void *p1 = mqtt_mempool_alloc(&pool);
    void *p2 = mqtt_mempool_alloc(&pool);
    mqtt_mempool_free(&pool, p1);

    mqtt_mempool_stats_t stats;
    mqtt_mempool_get_stats(&pool, &stats);
    ASSERT(stats.alloc_count == 2);
    ASSERT(stats.free_count == 1);

    /* Reset stats */
    mqtt_mempool_reset_stats(&pool);

    mqtt_mempool_get_stats(&pool, &stats);
    ASSERT(stats.alloc_count == 0);
    ASSERT(stats.free_count == 0);
    ASSERT(stats.used_blocks == 1);  /* Still tracking current usage */
    ASSERT(stats.peak_blocks == 1);  /* Reset to current */

    mqtt_mempool_free(&pool, p2);
    mqtt_mempool_cleanup(&pool);

    return 1;
}

/*******************************************************************************
 * Main
 ******************************************************************************/

int main(void)
{
    printf("Memory Pool Tests\n");
    printf("=================\n\n");

    printf("Running tests:\n");

    TEST(init_cleanup);
    TEST(alloc_free);
    TEST(exhaust_pool);
    TEST(peak_tracking);
    TEST(is_full);
    TEST(alignment);
    TEST(invalid_free);
    TEST(reset_stats);

    printf("\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
