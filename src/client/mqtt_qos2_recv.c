/**
 * @file mqtt_qos2_recv.c
 * @brief QoS 2 Receive State Tracker Implementation
 */

#include "mqtt_qos2_recv.h"
#include "../memory/mqtt_memory.h"
#include <string.h>

void mqtt_qos2_recv_init(mqtt_qos2_recv_tracker_t *tracker, size_t max_count)
{
    if (!tracker) {
        return;
    }

    memset(tracker, 0, sizeof(mqtt_qos2_recv_tracker_t));
    tracker->max_count = max_count;
}

void mqtt_qos2_recv_cleanup(mqtt_qos2_recv_tracker_t *tracker)
{
    mqtt_qos2_recv_clear(tracker);
}

void mqtt_qos2_recv_clear(mqtt_qos2_recv_tracker_t *tracker)
{
    if (!tracker) {
        return;
    }

    mqtt_qos2_recv_entry_t *entry = tracker->head;
    while (entry) {
        mqtt_qos2_recv_entry_t *next = entry->next;
        mqtt_free(entry);
        entry = next;
    }

    tracker->head = NULL;
    tracker->count = 0;
}

bool mqtt_qos2_recv_is_tracked(const mqtt_qos2_recv_tracker_t *tracker, uint16_t packet_id)
{
    if (!tracker) {
        return false;
    }

    mqtt_qos2_recv_entry_t *entry = tracker->head;
    while (entry) {
        if (entry->packet_id == packet_id) {
            return true;
        }
        entry = entry->next;
    }

    return false;
}

bool mqtt_qos2_recv_add(mqtt_qos2_recv_tracker_t *tracker, uint16_t packet_id)
{
    if (!tracker) {
        return false;
    }

    /* Check if already tracked */
    if (mqtt_qos2_recv_is_tracked(tracker, packet_id)) {
        return false;
    }

    /* Check capacity */
    if (tracker->max_count > 0 && tracker->count >= tracker->max_count) {
        return false;
    }

    /* Create new entry */
    mqtt_qos2_recv_entry_t *entry = mqtt_malloc(sizeof(mqtt_qos2_recv_entry_t));
    if (!entry) {
        return false;
    }

    entry->packet_id = packet_id;
    entry->state = MQTT_QOS2_RECV_PUBREC_SENT;
    entry->next = tracker->head;
    tracker->head = entry;
    tracker->count++;

    return true;
}

bool mqtt_qos2_recv_remove(mqtt_qos2_recv_tracker_t *tracker, uint16_t packet_id)
{
    if (!tracker) {
        return false;
    }

    mqtt_qos2_recv_entry_t *prev = NULL;
    mqtt_qos2_recv_entry_t *entry = tracker->head;

    while (entry) {
        if (entry->packet_id == packet_id) {
            /* Remove from list */
            if (prev) {
                prev->next = entry->next;
            } else {
                tracker->head = entry->next;
            }

            mqtt_free(entry);
            tracker->count--;
            return true;
        }

        prev = entry;
        entry = entry->next;
    }

    return false;
}

size_t mqtt_qos2_recv_count(const mqtt_qos2_recv_tracker_t *tracker)
{
    if (!tracker) {
        return 0;
    }

    return tracker->count;
}
