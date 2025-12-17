/**
 * @file mqtt_time_posix.c
 * @brief POSIX Time Implementation for MQTT Client Library
 *
 * This file provides the POSIX implementation of time functions using
 * clock_gettime() for monotonic time measurement and nanosleep() for
 * precise sleeping. It handles platform differences between Linux and macOS.
 */

#define _POSIX_C_SOURCE 200809L

#include "../mqtt_platform.h"
#include <time.h>
#include <errno.h>

/*******************************************************************************
 * Platform-Specific Clock Selection
 ******************************************************************************/

/**
 * Determine which clock to use for monotonic time:
 * - CLOCK_MONOTONIC_RAW: Linux - not affected by NTP adjustments
 * - CLOCK_MONOTONIC: POSIX standard - may be affected by NTP adjustments
 * - CLOCK_UPTIME: Some BSDs - monotonic time since boot
 */
#if defined(__linux__)
    #define MQTT_MONOTONIC_CLOCK CLOCK_MONOTONIC_RAW
#elif defined(CLOCK_MONOTONIC)
    #define MQTT_MONOTONIC_CLOCK CLOCK_MONOTONIC
#else
    #error "No monotonic clock available on this platform"
#endif

/*******************************************************************************
 * Time Functions
 ******************************************************************************/

uint64_t mqtt_time_monotonic_ms(void) {
    struct timespec ts;

    /* Get monotonic time */
    if (clock_gettime(MQTT_MONOTONIC_CLOCK, &ts) != 0) {
        /* Fallback to CLOCK_MONOTONIC if CLOCK_MONOTONIC_RAW fails */
#if MQTT_MONOTONIC_CLOCK != CLOCK_MONOTONIC
        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
            /* Last resort: use real time (not ideal but better than nothing) */
            clock_gettime(CLOCK_REALTIME, &ts);
        }
#else
        /* Last resort: use real time (not ideal but better than nothing) */
        clock_gettime(CLOCK_REALTIME, &ts);
#endif
    }

    /* Convert to milliseconds */
    uint64_t ms = (uint64_t)ts.tv_sec * 1000ULL;
    ms += (uint64_t)ts.tv_nsec / 1000000ULL;

    return ms;
}

void mqtt_sleep_ms(uint32_t ms) {
    if (ms == 0) {
        return;
    }

    struct timespec req, rem;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;

    /* Use nanosleep with automatic retry on EINTR (interrupted by signal) */
    while (nanosleep(&req, &rem) != 0) {
        if (errno != EINTR) {
            /* Not interrupted by signal - some other error occurred */
            break;
        }

        /* Sleep was interrupted - retry with remaining time */
        req = rem;
    }
}
