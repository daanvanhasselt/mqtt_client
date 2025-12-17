/**
 * @file mqtt_memory.c
 * @brief Memory Management Implementation
 */

#include "mqtt_memory.h"
#include <mqtt/mqtt_error.h>
#include <stdlib.h>
#include <string.h>

#ifdef MQTT_THREAD_SAFE
    #ifdef MQTT_PLATFORM_WINDOWS
        #include <windows.h>
        static CRITICAL_SECTION g_alloc_lock;
        static volatile LONG g_alloc_initialized = 0;
    #else
        #include <pthread.h>
        static pthread_mutex_t g_alloc_lock = PTHREAD_MUTEX_INITIALIZER;
        static volatile int g_alloc_initialized = 0;
    #endif
#endif

/* ========================================================================== */
/* Static Global State                                                        */
/* ========================================================================== */

/**
 * @brief Global custom allocator
 *
 * If NULL, the library uses standard malloc/free functions.
 * If set, all allocations are dispatched through the custom allocator.
 */
static const mqtt_allocator_t *g_allocator = NULL;

/**
 * @brief Initialization flag
 *
 * Tracks whether the memory system has been initialized.
 */
static int g_memory_initialized = 0;

/* ========================================================================== */
/* Internal Helper Functions                                                  */
/* ========================================================================== */

#ifdef MQTT_THREAD_SAFE

/**
 * @brief Initialize thread-safe locks
 */
static void init_locks(void) {
#ifdef MQTT_PLATFORM_WINDOWS
    if (InterlockedCompareExchange(&g_alloc_initialized, 1, 0) == 0) {
        InitializeCriticalSection(&g_alloc_lock);
    }
#else
    /* pthread_mutex_t is initialized statically */
    g_alloc_initialized = 1;
#endif
}

/**
 * @brief Lock the allocator mutex
 */
static void lock_allocator(void) {
#ifdef MQTT_PLATFORM_WINDOWS
    EnterCriticalSection(&g_alloc_lock);
#else
    pthread_mutex_lock(&g_alloc_lock);
#endif
}

/**
 * @brief Unlock the allocator mutex
 */
static void unlock_allocator(void) {
#ifdef MQTT_PLATFORM_WINDOWS
    LeaveCriticalSection(&g_alloc_lock);
#else
    pthread_mutex_unlock(&g_alloc_lock);
#endif
}

#endif /* MQTT_THREAD_SAFE */

/* ========================================================================== */
/* Public API Implementation                                                  */
/* ========================================================================== */

void mqtt_memory_init(const mqtt_allocator_t *alloc) {
#ifdef MQTT_THREAD_SAFE
    init_locks();
    lock_allocator();
#endif

    if (!g_memory_initialized) {
        g_allocator = alloc;
        g_memory_initialized = 1;
    }

#ifdef MQTT_THREAD_SAFE
    unlock_allocator();
#endif
}

void mqtt_memory_cleanup(void) {
#ifdef MQTT_THREAD_SAFE
    lock_allocator();
#endif

    g_allocator = NULL;
    g_memory_initialized = 0;

#ifdef MQTT_THREAD_SAFE
    unlock_allocator();
#ifdef MQTT_PLATFORM_WINDOWS
    if (InterlockedCompareExchange(&g_alloc_initialized, 0, 1) == 1) {
        DeleteCriticalSection(&g_alloc_lock);
    }
#endif
#endif
}

void *mqtt_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    if (g_allocator && g_allocator->malloc_fn) {
        return g_allocator->malloc_fn(size, g_allocator->ctx);
    }

    return malloc(size);
}

void *mqtt_realloc(void *ptr, size_t size) {
    if (g_allocator && g_allocator->realloc_fn) {
        return g_allocator->realloc_fn(ptr, size, g_allocator->ctx);
    }

    return realloc(ptr, size);
}

void mqtt_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    if (g_allocator && g_allocator->free_fn) {
        g_allocator->free_fn(ptr, g_allocator->ctx);
        return;
    }

    free(ptr);
}

void *mqtt_calloc(size_t count, size_t size) {
    if (count == 0 || size == 0) {
        return NULL;
    }

    if (g_allocator) {
        /* Custom allocator doesn't have calloc, so we emulate it */
        size_t total_size = count * size;
        void *ptr = mqtt_malloc(total_size);
        if (ptr) {
            memset(ptr, 0, total_size);
        }
        return ptr;
    }

    return calloc(count, size);
}

char *mqtt_strdup(const char *str) {
    if (str == NULL) {
        return NULL;
    }

    size_t len = strlen(str) + 1;
    char *dup = (char *)mqtt_malloc(len);
    if (dup) {
        memcpy(dup, str, len);
    }

    return dup;
}
