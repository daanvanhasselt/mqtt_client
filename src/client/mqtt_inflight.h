/**
 * @file mqtt_inflight.h
 * @brief MQTT Inflight Message Queue
 *
 * Manages inflight (unacknowledged) messages for QoS 1 and QoS 2.
 * Tracks message state through the acknowledgment handshake and
 * supports retry on timeout.
 */

#ifndef MQTT_INFLIGHT_H
#define MQTT_INFLIGHT_H

#include "mqtt/mqtt_error.h"
#include "mqtt/mqtt_types.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inflight message state for QoS handshake tracking
 */
typedef enum mqtt_inflight_state {
    MQTT_INFLIGHT_PENDING = 0,    /**< PUBLISH sent, awaiting ACK (QoS 1: PUBACK, QoS 2: PUBREC) */
    MQTT_INFLIGHT_PUBREC,         /**< QoS 2: PUBREC received, need to send PUBREL */
    MQTT_INFLIGHT_PUBREL,         /**< QoS 2: PUBREL sent, awaiting PUBCOMP */
} mqtt_inflight_state_t;

/**
 * @brief Inflight message entry
 *
 * Stores all information needed to track and potentially retransmit a message.
 */
typedef struct mqtt_inflight_entry {
    /* Identification */
    uint16_t packet_id;              /**< MQTT packet identifier */
    mqtt_qos_t qos;                  /**< QoS level (1 or 2) */
    mqtt_inflight_state_t state;     /**< Current state in ACK handshake */

    /* Timing */
    uint64_t send_time;              /**< Time when last sent (for retry) */
    uint8_t retry_count;             /**< Number of retransmission attempts */

    /* Message data (for retransmission) */
    char *topic;                     /**< Topic name (allocated copy) */
    uint8_t *payload;                /**< Payload data (allocated copy) */
    size_t payload_len;              /**< Payload length */
    bool retain;                     /**< Retain flag */

    /* Linked list */
    struct mqtt_inflight_entry *next;
    struct mqtt_inflight_entry *prev;
} mqtt_inflight_entry_t;

/**
 * @brief Inflight message queue
 */
typedef struct mqtt_inflight_queue {
    mqtt_inflight_entry_t *head;     /**< First entry (oldest) */
    mqtt_inflight_entry_t *tail;     /**< Last entry (newest) */
    size_t count;                    /**< Number of entries */
    size_t max_count;                /**< Maximum allowed entries */
    uint32_t retry_timeout_ms;       /**< Timeout before retry (default: 30000) */
    uint8_t max_retries;             /**< Maximum retry attempts (default: 3) */
} mqtt_inflight_queue_t;

/**
 * @brief Initialize the inflight queue
 *
 * @param queue Pointer to queue to initialize
 * @param max_inflight Maximum number of inflight messages (0 = unlimited)
 * @return MQTT_OK on success
 */
mqtt_error_t mqtt_inflight_init(mqtt_inflight_queue_t *queue, size_t max_inflight);

/**
 * @brief Cleanup the inflight queue and free all entries
 *
 * @param queue Pointer to queue to cleanup
 */
void mqtt_inflight_cleanup(mqtt_inflight_queue_t *queue);

/**
 * @brief Clear all entries from the queue without freeing the queue itself
 *
 * @param queue Pointer to queue to clear
 */
void mqtt_inflight_clear(mqtt_inflight_queue_t *queue);

/**
 * @brief Add a new inflight message to the queue
 *
 * @param queue Pointer to queue
 * @param packet_id Packet identifier
 * @param qos QoS level (must be 1 or 2)
 * @param topic Topic name (will be copied)
 * @param payload Payload data (will be copied)
 * @param payload_len Payload length
 * @param retain Retain flag
 * @param send_time Timestamp when message was sent
 * @return MQTT_OK on success, MQTT_ERR_INFLIGHT_FULL if queue is full
 */
mqtt_error_t mqtt_inflight_add(mqtt_inflight_queue_t *queue,
                               uint16_t packet_id,
                               mqtt_qos_t qos,
                               const char *topic,
                               const uint8_t *payload,
                               size_t payload_len,
                               bool retain,
                               uint64_t send_time);

/**
 * @brief Find an inflight entry by packet ID
 *
 * @param queue Pointer to queue
 * @param packet_id Packet identifier to find
 * @return Pointer to entry, or NULL if not found
 */
mqtt_inflight_entry_t *mqtt_inflight_find(mqtt_inflight_queue_t *queue, uint16_t packet_id);

/**
 * @brief Remove an entry from the queue
 *
 * @param queue Pointer to queue
 * @param entry Entry to remove (must be in the queue)
 */
void mqtt_inflight_remove(mqtt_inflight_queue_t *queue, mqtt_inflight_entry_t *entry);

/**
 * @brief Update entry state (for QoS 2 state machine)
 *
 * @param entry Entry to update
 * @param new_state New state
 * @param send_time Timestamp for the state transition
 */
void mqtt_inflight_update_state(mqtt_inflight_entry_t *entry,
                                mqtt_inflight_state_t new_state,
                                uint64_t send_time);

/**
 * @brief Get first entry that needs retry (timed out)
 *
 * Scans the queue for entries whose send_time + retry_timeout_ms < current_time
 * and retry_count < max_retries.
 *
 * @param queue Pointer to queue
 * @param current_time Current timestamp in milliseconds
 * @return Entry needing retry, or NULL if none
 */
mqtt_inflight_entry_t *mqtt_inflight_get_retry(mqtt_inflight_queue_t *queue, uint64_t current_time);

/**
 * @brief Mark entry as retried (increment counter, update send time)
 *
 * @param entry Entry that was retried
 * @param send_time New send timestamp
 */
void mqtt_inflight_mark_retried(mqtt_inflight_entry_t *entry, uint64_t send_time);

/**
 * @brief Check if queue is full
 *
 * @param queue Pointer to queue
 * @return true if full (or max_count is set and reached), false otherwise
 */
bool mqtt_inflight_is_full(const mqtt_inflight_queue_t *queue);

/**
 * @brief Get number of entries in queue
 *
 * @param queue Pointer to queue
 * @return Number of entries
 */
size_t mqtt_inflight_count(const mqtt_inflight_queue_t *queue);

/**
 * @brief Set retry configuration
 *
 * @param queue Pointer to queue
 * @param timeout_ms Retry timeout in milliseconds
 * @param max_retries Maximum retry attempts
 */
void mqtt_inflight_set_retry_config(mqtt_inflight_queue_t *queue,
                                    uint32_t timeout_ms,
                                    uint8_t max_retries);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_INFLIGHT_H */
