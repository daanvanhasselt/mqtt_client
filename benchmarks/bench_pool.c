/**
 * @file bench_pool.c
 * @brief Memory pool allocator benchmark
 *
 * Measures allocation/deallocation performance of the memory pool
 * compared to standard malloc/free.
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/memory/mqtt_pool.h"

#define ITERATIONS      1000000
#define BLOCK_SIZE      64
#define POOL_SIZE       1000

/*******************************************************************************
 * Timing Helpers
 ******************************************************************************/

static double get_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/*******************************************************************************
 * Benchmarks
 ******************************************************************************/

static void bench_pool_sequential(void)
{
    mqtt_mempool_t pool;
    mqtt_mempool_init(&pool, BLOCK_SIZE, POOL_SIZE);

    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        void *ptr = mqtt_mempool_alloc(&pool);
        mqtt_mempool_free(&pool, ptr);
    }

    double elapsed = get_time_sec() - start;

    printf("Pool sequential alloc/free:   %d iterations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS, elapsed, ITERATIONS / elapsed);

    mqtt_mempool_cleanup(&pool);
}

static void bench_malloc_sequential(void)
{
    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        void *ptr = malloc(BLOCK_SIZE);
        free(ptr);
    }

    double elapsed = get_time_sec() - start;

    printf("Malloc sequential alloc/free: %d iterations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS, elapsed, ITERATIONS / elapsed);
}

static void bench_pool_batch(void)
{
    mqtt_mempool_t pool;
    mqtt_mempool_init(&pool, BLOCK_SIZE, POOL_SIZE);

    void *ptrs[POOL_SIZE];
    int batch_count = ITERATIONS / POOL_SIZE;

    double start = get_time_sec();

    for (int batch = 0; batch < batch_count; batch++) {
        /* Allocate all blocks */
        for (int i = 0; i < POOL_SIZE; i++) {
            ptrs[i] = mqtt_mempool_alloc(&pool);
        }
        /* Free all blocks */
        for (int i = 0; i < POOL_SIZE; i++) {
            mqtt_mempool_free(&pool, ptrs[i]);
        }
    }

    double elapsed = get_time_sec() - start;
    int total_ops = batch_count * POOL_SIZE * 2;

    printf("Pool batch alloc/free:        %d operations in %.3f sec (%.0f ops/sec)\n",
           total_ops, elapsed, total_ops / elapsed);

    mqtt_mempool_cleanup(&pool);
}

static void bench_malloc_batch(void)
{
    void *ptrs[POOL_SIZE];
    int batch_count = ITERATIONS / POOL_SIZE;

    double start = get_time_sec();

    for (int batch = 0; batch < batch_count; batch++) {
        /* Allocate all blocks */
        for (int i = 0; i < POOL_SIZE; i++) {
            ptrs[i] = malloc(BLOCK_SIZE);
        }
        /* Free all blocks */
        for (int i = 0; i < POOL_SIZE; i++) {
            free(ptrs[i]);
        }
    }

    double elapsed = get_time_sec() - start;
    int total_ops = batch_count * POOL_SIZE * 2;

    printf("Malloc batch alloc/free:      %d operations in %.3f sec (%.0f ops/sec)\n",
           total_ops, elapsed, total_ops / elapsed);
}

static void bench_pool_mixed(void)
{
    mqtt_mempool_t pool;
    mqtt_mempool_init(&pool, BLOCK_SIZE, POOL_SIZE);

    void *ptrs[POOL_SIZE];
    int head = 0;
    int count = 0;

    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        /* 50% allocate, 50% free (when available) */
        if (count < POOL_SIZE && (count == 0 || (rand() % 2) == 0)) {
            ptrs[head] = mqtt_mempool_alloc(&pool);
            head = (head + 1) % POOL_SIZE;
            count++;
        } else if (count > 0) {
            int idx = (head - count + POOL_SIZE) % POOL_SIZE;
            mqtt_mempool_free(&pool, ptrs[idx]);
            count--;
        }
    }

    double elapsed = get_time_sec() - start;

    printf("Pool mixed alloc/free:        %d operations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS, elapsed, ITERATIONS / elapsed);

    /* Cleanup remaining allocations */
    while (count > 0) {
        int idx = (head - count + POOL_SIZE) % POOL_SIZE;
        mqtt_mempool_free(&pool, ptrs[idx]);
        count--;
    }

    mqtt_mempool_cleanup(&pool);
}

/*******************************************************************************
 * Main
 ******************************************************************************/

int main(void)
{
    printf("Memory Pool Allocator Benchmark\n");
    printf("================================\n");
    printf("Block size: %d bytes, Pool size: %d blocks\n\n", BLOCK_SIZE, POOL_SIZE);

    /* Seed random for mixed benchmark */
    srand((unsigned int)time(NULL));

    printf("Sequential allocation (alloc then immediately free):\n");
    bench_pool_sequential();
    bench_malloc_sequential();
    printf("\n");

    printf("Batch allocation (alloc all, then free all):\n");
    bench_pool_batch();
    bench_malloc_batch();
    printf("\n");

    printf("Mixed allocation (random alloc/free pattern):\n");
    bench_pool_mixed();
    printf("\n");

    return 0;
}
