/**
 * @file mqtt_inflight.c
 * @brief MQTT Inflight Message Queue Implementation
 */

#include "mqtt_inflight.h"
#include "../memory/mqtt_memory.h"
#include <string.h>

/* Default retry configuration */
#define DEFAULT_RETRY_TIMEOUT_MS  30000
#define DEFAULT_MAX_RETRIES       3

/**
 * @brief Free an inflight entry and its allocated data
 */
static void mqtt_inflight_entry_free(mqtt_inflight_entry_t *entry)
{
    if (!entry) {
        return;
    }

    if (entry->topic) {
        mqtt_free(entry->topic);
    }
    if (entry->payload) {
        mqtt_free(entry->payload);
    }
    mqtt_free(entry);
}

mqtt_error_t mqtt_inflight_init(mqtt_inflight_queue_t *queue, size_t max_inflight)
{
    if (!queue) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(queue, 0, sizeof(mqtt_inflight_queue_t));
    queue->max_count = max_inflight;
    queue->retry_timeout_ms = DEFAULT_RETRY_TIMEOUT_MS;
    queue->max_retries = DEFAULT_MAX_RETRIES;

    return MQTT_OK;
}

void mqtt_inflight_cleanup(mqtt_inflight_queue_t *queue)
{
    if (!queue) {
        return;
    }

    mqtt_inflight_clear(queue);
}

void mqtt_inflight_clear(mqtt_inflight_queue_t *queue)
{
    if (!queue) {
        return;
    }

    mqtt_inflight_entry_t *entry = queue->head;
    while (entry) {
        mqtt_inflight_entry_t *next = entry->next;
        mqtt_inflight_entry_free(entry);
        entry = next;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
}

mqtt_error_t mqtt_inflight_add(mqtt_inflight_queue_t *queue,
                               uint16_t packet_id,
                               mqtt_qos_t qos,
                               const char *topic,
                               const uint8_t *payload,
                               size_t payload_len,
                               bool retain,
                               uint64_t send_time)
{
    if (!queue || !topic) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* QoS 0 doesn't need inflight tracking */
    if (qos == MQTT_QOS_0) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Check if queue is full */
    if (mqtt_inflight_is_full(queue)) {
        return MQTT_ERR_INFLIGHT_FULL;
    }

    /* Allocate entry */
    mqtt_inflight_entry_t *entry = mqtt_malloc(sizeof(mqtt_inflight_entry_t));
    if (!entry) {
        return MQTT_ERR_NOMEM;
    }

    memset(entry, 0, sizeof(mqtt_inflight_entry_t));

    /* Copy topic */
    size_t topic_len = strlen(topic);
    entry->topic = mqtt_malloc(topic_len + 1);
    if (!entry->topic) {
        mqtt_free(entry);
        return MQTT_ERR_NOMEM;
    }
    memcpy(entry->topic, topic, topic_len + 1);

    /* Copy payload if present */
    if (payload && payload_len > 0) {
        entry->payload = mqtt_malloc(payload_len);
        if (!entry->payload) {
            mqtt_free(entry->topic);
            mqtt_free(entry);
            return MQTT_ERR_NOMEM;
        }
        memcpy(entry->payload, payload, payload_len);
        entry->payload_len = payload_len;
    }

    /* Initialize entry */
    entry->packet_id = packet_id;
    entry->qos = qos;
    entry->state = MQTT_INFLIGHT_PENDING;
    entry->send_time = send_time;
    entry->retry_count = 0;
    entry->retain = retain;

    /* Add to end of queue (doubly-linked list) */
    entry->prev = queue->tail;
    entry->next = NULL;

    if (queue->tail) {
        queue->tail->next = entry;
    } else {
        queue->head = entry;
    }
    queue->tail = entry;
    queue->count++;

    return MQTT_OK;
}

mqtt_inflight_entry_t *mqtt_inflight_find(mqtt_inflight_queue_t *queue, uint16_t packet_id)
{
    if (!queue) {
        return NULL;
    }

    mqtt_inflight_entry_t *entry = queue->head;
    while (entry) {
        if (entry->packet_id == packet_id) {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

void mqtt_inflight_remove(mqtt_inflight_queue_t *queue, mqtt_inflight_entry_t *entry)
{
    if (!queue || !entry) {
        return;
    }

    /* Update linked list */
    if (entry->prev) {
        entry->prev->next = entry->next;
    } else {
        queue->head = entry->next;
    }

    if (entry->next) {
        entry->next->prev = entry->prev;
    } else {
        queue->tail = entry->prev;
    }

    queue->count--;

    /* Free entry */
    mqtt_inflight_entry_free(entry);
}

void mqtt_inflight_update_state(mqtt_inflight_entry_t *entry,
                                mqtt_inflight_state_t new_state,
                                uint64_t send_time)
{
    if (!entry) {
        return;
    }

    entry->state = new_state;
    entry->send_time = send_time;
    entry->retry_count = 0;  /* Reset retry count on state transition */
}

mqtt_inflight_entry_t *mqtt_inflight_get_retry(mqtt_inflight_queue_t *queue, uint64_t current_time)
{
    if (!queue) {
        return NULL;
    }

    mqtt_inflight_entry_t *entry = queue->head;
    while (entry) {
        /* Check if timed out and has retries remaining */
        if (entry->retry_count < queue->max_retries) {
            uint64_t elapsed = current_time - entry->send_time;
            if (elapsed >= queue->retry_timeout_ms) {
                return entry;
            }
        }
        entry = entry->next;
    }

    return NULL;
}

void mqtt_inflight_mark_retried(mqtt_inflight_entry_t *entry, uint64_t send_time)
{
    if (!entry) {
        return;
    }

    entry->retry_count++;
    entry->send_time = send_time;
}

bool mqtt_inflight_is_full(const mqtt_inflight_queue_t *queue)
{
    if (!queue) {
        return true;
    }

    /* If max_count is 0, queue is unlimited */
    if (queue->max_count == 0) {
        return false;
    }

    return queue->count >= queue->max_count;
}

size_t mqtt_inflight_count(const mqtt_inflight_queue_t *queue)
{
    if (!queue) {
        return 0;
    }

    return queue->count;
}

void mqtt_inflight_set_retry_config(mqtt_inflight_queue_t *queue,
                                    uint32_t timeout_ms,
                                    uint8_t max_retries)
{
    if (!queue) {
        return;
    }

    queue->retry_timeout_ms = timeout_ms;
    queue->max_retries = max_retries;
}

mqtt_inflight_entry_t *mqtt_inflight_get_expired(mqtt_inflight_queue_t *queue)
{
    if (!queue) {
        return NULL;
    }

    mqtt_inflight_entry_t *entry = queue->head;
    while (entry) {
        if (entry->retry_count >= queue->max_retries) {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}
