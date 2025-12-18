/**
 * @file mqtt_thread_windows.c
 * @brief Windows Threading Primitives Implementation
 *
 * Provides mutex operations using Windows Critical Sections.
 */

#if defined(_WIN32) || defined(_WIN64)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../mqtt_platform.h"
#include <mqtt/mqtt_config.h>

#ifdef MQTT_THREAD_SAFE

/*******************************************************************************
 * Mutex Functions
 ******************************************************************************/

mqtt_error_t mqtt_mutex_init(mqtt_mutex_t *mutex) {
    if (mutex == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    InitializeCriticalSection(mutex);
    return MQTT_OK;
}

void mqtt_mutex_destroy(mqtt_mutex_t *mutex) {
    if (mutex != NULL) {
        DeleteCriticalSection(mutex);
    }
}

void mqtt_mutex_lock(mqtt_mutex_t *mutex) {
    if (mutex != NULL) {
        EnterCriticalSection(mutex);
    }
}

void mqtt_mutex_unlock(mqtt_mutex_t *mutex) {
    if (mutex != NULL) {
        LeaveCriticalSection(mutex);
    }
}

#endif /* MQTT_THREAD_SAFE */

#endif /* _WIN32 || _WIN64 */
