/**
 * @file mqtt_time_windows.c
 * @brief Windows Time Functions Implementation
 *
 * Provides high-resolution monotonic time using QueryPerformanceCounter
 * and sleep functionality using Sleep().
 */

#if defined(_WIN32) || defined(_WIN64)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../mqtt_platform.h"

/*******************************************************************************
 * Time Functions
 ******************************************************************************/

uint64_t mqtt_time_monotonic_ms(void) {
    static LARGE_INTEGER frequency = {0};
    static int initialized = 0;

    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        initialized = 1;
    }

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    /* Convert to milliseconds */
    return (uint64_t)(counter.QuadPart * 1000 / frequency.QuadPart);
}

void mqtt_sleep_ms(uint32_t ms) {
    Sleep(ms);
}

#endif /* _WIN32 || _WIN64 */
