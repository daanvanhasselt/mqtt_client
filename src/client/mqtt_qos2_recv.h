/**
 * @file mqtt_qos2_recv.h
 * @brief QoS 2 Receive State Tracker
 *
 * Tracks received QoS 2 messages to prevent duplicate delivery.
 * When we receive a QoS 2 PUBLISH:
 * 1. Check if packet_id is already tracked (duplicate) - resend PUBREC, don't deliver
 * 2. If new, deliver message, add to tracker, send PUBREC
 * 3. When PUBREL arrives, send PUBCOMP and remove from tracker
 */

#ifndef MQTT_QOS2_RECV_H
#define MQTT_QOS2_RECV_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief QoS 2 receive state for a single message
 */
typedef enum mqtt_qos2_recv_state {
    MQTT_QOS2_RECV_PUBREC_SENT = 0,  /**< PUBREC sent, waiting for PUBREL */
} mqtt_qos2_recv_state_t;

/**
 * @brief Entry in the QoS 2 receive tracker
 */
typedef struct mqtt_qos2_recv_entry {
    uint16_t packet_id;
    mqtt_qos2_recv_state_t state;
    struct mqtt_qos2_recv_entry *next;
} mqtt_qos2_recv_entry_t;

/**
 * @brief QoS 2 receive tracker
 *
 * Simple linked list of received QoS 2 packet IDs awaiting PUBREL.
 */
typedef struct mqtt_qos2_recv_tracker {
    mqtt_qos2_recv_entry_t *head;
    size_t count;
    size_t max_count;  /**< 0 = unlimited */
} mqtt_qos2_recv_tracker_t;

/**
 * @brief Initialize QoS 2 receive tracker
 *
 * @param tracker Pointer to tracker to initialize
 * @param max_count Maximum number of tracked entries (0 = unlimited)
 */
void mqtt_qos2_recv_init(mqtt_qos2_recv_tracker_t *tracker, size_t max_count);

/**
 * @brief Cleanup QoS 2 receive tracker
 *
 * @param tracker Pointer to tracker to cleanup
 */
void mqtt_qos2_recv_cleanup(mqtt_qos2_recv_tracker_t *tracker);

/**
 * @brief Clear all entries from the tracker
 *
 * @param tracker Pointer to tracker to clear
 */
void mqtt_qos2_recv_clear(mqtt_qos2_recv_tracker_t *tracker);

/**
 * @brief Check if a packet ID is being tracked (already received)
 *
 * @param tracker Pointer to tracker
 * @param packet_id Packet ID to check
 * @return true if packet ID is tracked (duplicate), false if new
 */
bool mqtt_qos2_recv_is_tracked(const mqtt_qos2_recv_tracker_t *tracker, uint16_t packet_id);

/**
 * @brief Add a packet ID to the tracker (new QoS 2 message received)
 *
 * @param tracker Pointer to tracker
 * @param packet_id Packet ID to add
 * @return true on success, false if already tracked or at capacity
 */
bool mqtt_qos2_recv_add(mqtt_qos2_recv_tracker_t *tracker, uint16_t packet_id);

/**
 * @brief Remove a packet ID from the tracker (PUBREL received, transaction complete)
 *
 * @param tracker Pointer to tracker
 * @param packet_id Packet ID to remove
 * @return true if removed, false if not found
 */
bool mqtt_qos2_recv_remove(mqtt_qos2_recv_tracker_t *tracker, uint16_t packet_id);

/**
 * @brief Get the number of tracked entries
 *
 * @param tracker Pointer to tracker
 * @return Number of entries
 */
size_t mqtt_qos2_recv_count(const mqtt_qos2_recv_tracker_t *tracker);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_QOS2_RECV_H */
