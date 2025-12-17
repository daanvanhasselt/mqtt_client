/**
 * @file mqtt_buffer.c
 * @brief Dynamic Buffer Implementation
 */

#include "mqtt_buffer.h"
#include "mqtt_memory.h"
#include <string.h>
#include <mqtt/mqtt_error.h>

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

/**
 * @brief Default initial capacity for buffers (if not specified)
 */
#define DEFAULT_BUFFER_CAPACITY 256

/**
 * @brief Minimum growth amount when expanding buffer (bytes)
 */
#define MIN_GROWTH_SIZE 128

/**
 * @brief Maximum buffer size to prevent runaway allocations
 */
#define MAX_BUFFER_SIZE (64 * 1024 * 1024)  /* 64 MB */

/* ========================================================================== */
/* Internal Helper Functions                                                  */
/* ========================================================================== */

/**
 * @brief Calculate the next buffer capacity using growth strategy
 *
 * Uses a 2x growth strategy with a minimum increment to balance
 * allocation overhead and memory waste.
 *
 * @param current Current capacity
 * @param required Minimum required capacity
 * @return New capacity (guaranteed >= required)
 */
static size_t calculate_new_capacity(size_t current, size_t required) {
    size_t new_capacity = current;

    /* Start with doubling */
    if (new_capacity < required) {
        new_capacity = current * 2;
    }

    /* Ensure minimum growth */
    if (new_capacity < current + MIN_GROWTH_SIZE) {
        new_capacity = current + MIN_GROWTH_SIZE;
    }

    /* Ensure we meet the requirement */
    if (new_capacity < required) {
        new_capacity = required;
    }

    /* Cap at maximum size */
    if (new_capacity > MAX_BUFFER_SIZE) {
        new_capacity = MAX_BUFFER_SIZE;
    }

    return new_capacity;
}

/**
 * @brief Grow the buffer to at least the specified capacity
 *
 * @param buf Buffer to grow
 * @param required Minimum required capacity
 * @return MQTT_OK on success, error code on failure
 */
static mqtt_error_t grow_buffer(mqtt_buffer_t *buf, size_t required) {
    if (!buf) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Check if already large enough */
    if (buf->capacity >= required) {
        return MQTT_OK;
    }

    /* Cannot grow static or readonly buffers */
    if (buf->flags & (MQTT_BUF_STATIC | MQTT_BUF_READONLY)) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Check against maximum size */
    if (required > MAX_BUFFER_SIZE) {
        return MQTT_ERR_NOMEM;
    }

    /* Calculate new capacity */
    size_t new_capacity = calculate_new_capacity(buf->capacity, required);

    /* Reallocate buffer */
    uint8_t *new_data;
    if (buf->flags & MQTT_BUF_OWNED) {
        new_data = (uint8_t *)MQTT_REALLOC(buf->data, new_capacity);
    } else {
        /* Buffer wasn't owned, allocate new and copy */
        new_data = (uint8_t *)MQTT_MALLOC(new_capacity);
        if (new_data && buf->data && buf->len > 0) {
            memcpy(new_data, buf->data, buf->len);
        }
    }

    if (!new_data) {
        return MQTT_ERR_NOMEM;
    }

    buf->data = new_data;
    buf->capacity = new_capacity;
    buf->flags |= MQTT_BUF_OWNED;

    return MQTT_OK;
}

/* ========================================================================== */
/* Public API Implementation                                                  */
/* ========================================================================== */

mqtt_error_t mqtt_buffer_init(mqtt_buffer_t *buf, size_t initial_capacity) {
    if (!buf) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Use default capacity if not specified */
    if (initial_capacity == 0) {
        initial_capacity = DEFAULT_BUFFER_CAPACITY;
    }

    /* Check against maximum size */
    if (initial_capacity > MAX_BUFFER_SIZE) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Allocate buffer */
    buf->data = (uint8_t *)MQTT_MALLOC(initial_capacity);
    if (!buf->data) {
        return MQTT_ERR_NOMEM;
    }

    buf->len = 0;
    buf->capacity = initial_capacity;
    buf->flags = MQTT_BUF_OWNED;

    return MQTT_OK;
}

mqtt_error_t mqtt_buffer_init_static(mqtt_buffer_t *buf, uint8_t *data, size_t capacity) {
    if (!buf || !data) {
        return MQTT_ERR_INVALID_ARG;
    }

    buf->data = data;
    buf->len = 0;
    buf->capacity = capacity;
    buf->flags = MQTT_BUF_STATIC;

    return MQTT_OK;
}

void mqtt_buffer_cleanup(mqtt_buffer_t *buf) {
    if (!buf) {
        return;
    }

    /* Free data if owned */
    if ((buf->flags & MQTT_BUF_OWNED) && buf->data) {
        MQTT_FREE(buf->data);
    }

    /* Reset structure */
    buf->data = NULL;
    buf->len = 0;
    buf->capacity = 0;
    buf->flags = 0;
}

mqtt_error_t mqtt_buffer_reserve(mqtt_buffer_t *buf, size_t capacity) {
    if (!buf) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Already have enough capacity */
    if (buf->capacity >= capacity) {
        return MQTT_OK;
    }

    /* Cannot resize static or readonly buffers */
    if (buf->flags & (MQTT_BUF_STATIC | MQTT_BUF_READONLY)) {
        return MQTT_ERR_INVALID_ARG;
    }

    return grow_buffer(buf, capacity);
}

mqtt_error_t mqtt_buffer_append(mqtt_buffer_t *buf, const void *data, size_t len) {
    if (!buf || (!data && len > 0)) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Nothing to append */
    if (len == 0) {
        return MQTT_OK;
    }

    /* Cannot write to readonly buffer */
    if (buf->flags & MQTT_BUF_READONLY) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Check if we need to grow */
    size_t required = buf->len + len;
    if (required > buf->capacity) {
        /* Static buffers cannot grow */
        if (buf->flags & MQTT_BUF_STATIC) {
            return MQTT_ERR_NOMEM;
        }

        mqtt_error_t err = grow_buffer(buf, required);
        if (err != MQTT_OK) {
            return err;
        }
    }

    /* Append data */
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;

    return MQTT_OK;
}

mqtt_error_t mqtt_buffer_prepend(mqtt_buffer_t *buf, const void *data, size_t len) {
    if (!buf || (!data && len > 0)) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Nothing to prepend */
    if (len == 0) {
        return MQTT_OK;
    }

    /* Cannot write to readonly buffer */
    if (buf->flags & MQTT_BUF_READONLY) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Check if we need to grow */
    size_t required = buf->len + len;
    if (required > buf->capacity) {
        /* Static buffers cannot grow */
        if (buf->flags & MQTT_BUF_STATIC) {
            return MQTT_ERR_NOMEM;
        }

        mqtt_error_t err = grow_buffer(buf, required);
        if (err != MQTT_OK) {
            return err;
        }
    }

    /* Shift existing data forward */
    if (buf->len > 0) {
        memmove(buf->data + len, buf->data, buf->len);
    }

    /* Copy new data to beginning */
    memcpy(buf->data, data, len);
    buf->len += len;

    return MQTT_OK;
}

void mqtt_buffer_consume(mqtt_buffer_t *buf, size_t len) {
    if (!buf || buf->len == 0) {
        return;
    }

    /* Consuming more than available empties the buffer */
    if (len >= buf->len) {
        buf->len = 0;
        return;
    }

    /* Shift remaining data forward */
    size_t remaining = buf->len - len;
    memmove(buf->data, buf->data + len, remaining);
    buf->len = remaining;
}

void mqtt_buffer_reset(mqtt_buffer_t *buf) {
    if (buf) {
        buf->len = 0;
    }
}

uint8_t *mqtt_buffer_write_ptr(mqtt_buffer_t *buf) {
    if (!buf || !buf->data) {
        return NULL;
    }

    /* Cannot write to readonly buffer */
    if (buf->flags & MQTT_BUF_READONLY) {
        return NULL;
    }

    /* No space available */
    if (buf->len >= buf->capacity) {
        return NULL;
    }

    return buf->data + buf->len;
}

size_t mqtt_buffer_write_available(mqtt_buffer_t *buf) {
    if (!buf) {
        return 0;
    }

    /* Cannot write to readonly buffer */
    if (buf->flags & MQTT_BUF_READONLY) {
        return 0;
    }

    /* No space available */
    if (buf->len >= buf->capacity) {
        return 0;
    }

    return buf->capacity - buf->len;
}

void mqtt_buffer_advance_write(mqtt_buffer_t *buf, size_t len) {
    if (!buf) {
        return;
    }

    /* Advance length, but cap at capacity to prevent corruption */
    size_t new_len = buf->len + len;
    if (new_len > buf->capacity) {
        new_len = buf->capacity;
    }

    buf->len = new_len;
}
