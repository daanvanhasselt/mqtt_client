/**
 * @file mqtt_packet_id.h
 * @brief MQTT Packet ID Allocator
 *
 * Manages allocation and deallocation of MQTT packet identifiers.
 * Packet IDs are 16-bit integers ranging from 1-65535 (0 is reserved).
 * Uses a bitmap for efficient O(1) allocation/deallocation tracking.
 */

#ifndef MQTT_PACKET_ID_H
#define MQTT_PACKET_ID_H

#include "mqtt/mqtt_error.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of packet IDs (1-65535)
 */
#define MQTT_PACKET_ID_MAX 65535

/**
 * @brief Bitmap size in bytes (65536 bits / 8 = 8192 bytes)
 */
#define MQTT_PACKET_ID_BITMAP_SIZE 8192

/**
 * @brief Packet ID allocator structure
 *
 * Uses a bitmap where each bit represents whether a packet ID is in use.
 * Bit 0 is unused (packet ID 0 is reserved), bits 1-65535 map to packet IDs.
 */
typedef struct mqtt_packet_id_allocator {
    uint8_t bitmap[MQTT_PACKET_ID_BITMAP_SIZE];  /**< Bitmap of allocated IDs */
    uint16_t next_hint;                           /**< Hint for next allocation */
    uint16_t allocated_count;                     /**< Number of allocated IDs */
} mqtt_packet_id_allocator_t;

/**
 * @brief Initialize the packet ID allocator
 *
 * @param allocator Pointer to allocator to initialize
 * @return MQTT_OK on success, error code on failure
 */
mqtt_error_t mqtt_packet_id_init(mqtt_packet_id_allocator_t *allocator);

/**
 * @brief Reset the packet ID allocator (free all IDs)
 *
 * @param allocator Pointer to allocator to reset
 */
void mqtt_packet_id_reset(mqtt_packet_id_allocator_t *allocator);

/**
 * @brief Allocate a new packet ID
 *
 * @param allocator Pointer to allocator
 * @param[out] packet_id Allocated packet ID (1-65535)
 * @return MQTT_OK on success, MQTT_ERR_PACKET_ID_EXHAUSTED if no IDs available
 */
mqtt_error_t mqtt_packet_id_alloc(mqtt_packet_id_allocator_t *allocator, uint16_t *packet_id);

/**
 * @brief Free a previously allocated packet ID
 *
 * @param allocator Pointer to allocator
 * @param packet_id Packet ID to free (1-65535)
 * @return MQTT_OK on success, MQTT_ERR_INVALID_PACKET_ID if ID was not allocated
 */
mqtt_error_t mqtt_packet_id_free(mqtt_packet_id_allocator_t *allocator, uint16_t packet_id);

/**
 * @brief Check if a packet ID is currently allocated
 *
 * @param allocator Pointer to allocator
 * @param packet_id Packet ID to check
 * @return true if allocated, false otherwise
 */
bool mqtt_packet_id_is_allocated(const mqtt_packet_id_allocator_t *allocator, uint16_t packet_id);

/**
 * @brief Get the number of currently allocated packet IDs
 *
 * @param allocator Pointer to allocator
 * @return Number of allocated IDs
 */
uint16_t mqtt_packet_id_count(const mqtt_packet_id_allocator_t *allocator);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_PACKET_ID_H */
