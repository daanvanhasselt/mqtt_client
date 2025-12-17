/**
 * @file mqtt_thread_posix.c
 * @brief POSIX Threading Implementation for MQTT Client Library
 *
 * This file provides the POSIX implementation of threading primitives using
 * pthread mutexes. It is only compiled when MQTT_THREAD_SAFE is defined.
 */

#include "../mqtt_platform.h"

#ifdef MQTT_THREAD_SAFE

#include <pthread.h>
#include <errno.h>

/*******************************************************************************
 * Mutex Functions
 ******************************************************************************/

mqtt_error_t mqtt_mutex_init(mqtt_mutex_t *mutex) {
    if (mutex == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Initialize mutex with default attributes */
    int result = pthread_mutex_init(mutex, NULL);

    switch (result) {
        case 0:
            return MQTT_OK;
        case ENOMEM:
            return MQTT_ERR_NOMEM;
        case EAGAIN:
            return MQTT_ERR_NOMEM;  /* Insufficient resources */
        case EINVAL:
            return MQTT_ERR_INVALID_ARG;
        default:
            return MQTT_ERR_INTERNAL;
    }
}

void mqtt_mutex_destroy(mqtt_mutex_t *mutex) {
    if (mutex == NULL) {
        return;
    }

    /* Destroy the mutex - ignore errors */
    pthread_mutex_destroy(mutex);
}

void mqtt_mutex_lock(mqtt_mutex_t *mutex) {
    if (mutex == NULL) {
        return;
    }

    /* Lock the mutex - this should never fail in normal operation */
    int result = pthread_mutex_lock(mutex);

    /* In debug builds, we could assert on failure */
    (void)result;  /* Suppress unused variable warning */
}

void mqtt_mutex_unlock(mqtt_mutex_t *mutex) {
    if (mutex == NULL) {
        return;
    }

    /* Unlock the mutex - this should never fail in normal operation */
    int result = pthread_mutex_unlock(mutex);

    /* In debug builds, we could assert on failure */
    (void)result;  /* Suppress unused variable warning */
}

#endif /* MQTT_THREAD_SAFE */
