/**
 * @file mqtt_packet_id.c
 * @brief MQTT Packet ID Allocator Implementation
 */

#include "mqtt_packet_id.h"
#include <string.h>

/* Bitmap manipulation macros */
#define BITMAP_BYTE(id) ((id) >> 3)        /* id / 8 */
#define BITMAP_BIT(id)  (1 << ((id) & 7))  /* 1 << (id % 8) */

mqtt_error_t mqtt_packet_id_init(mqtt_packet_id_allocator_t *allocator)
{
    if (!allocator) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(allocator->bitmap, 0, MQTT_PACKET_ID_BITMAP_SIZE);
    allocator->next_hint = 1;  /* Start at 1, not 0 */
    allocator->allocated_count = 0;

    /* Mark packet ID 0 as always allocated (reserved) */
    allocator->bitmap[0] |= 0x01;

    return MQTT_OK;
}

void mqtt_packet_id_reset(mqtt_packet_id_allocator_t *allocator)
{
    if (!allocator) {
        return;
    }

    memset(allocator->bitmap, 0, MQTT_PACKET_ID_BITMAP_SIZE);
    allocator->next_hint = 1;
    allocator->allocated_count = 0;

    /* Mark packet ID 0 as always allocated (reserved) */
    allocator->bitmap[0] |= 0x01;
}

mqtt_error_t mqtt_packet_id_alloc(mqtt_packet_id_allocator_t *allocator, uint16_t *packet_id)
{
    if (!allocator || !packet_id) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Check if we've exhausted all IDs */
    if (allocator->allocated_count >= MQTT_PACKET_ID_MAX) {
        return MQTT_ERR_PACKET_ID_EXHAUSTED;
    }

    /* Start searching from hint position */
    uint16_t start = allocator->next_hint;
    uint16_t id = start;

    do {
        /* Check if this ID is free */
        if (!(allocator->bitmap[BITMAP_BYTE(id)] & BITMAP_BIT(id))) {
            /* Found a free ID - allocate it */
            allocator->bitmap[BITMAP_BYTE(id)] |= BITMAP_BIT(id);
            allocator->allocated_count++;

            /* Update hint for next allocation */
            allocator->next_hint = id + 1;
            if (allocator->next_hint == 0) {
                allocator->next_hint = 1;
            }

            *packet_id = id;
            return MQTT_OK;
        }

        /* Move to next ID */
        id++;
        if (id == 0) {
            id = 1;  /* Skip 0, wrap to 1 */
        }
    } while (id != start);

    /* Should never reach here if allocated_count is accurate */
    return MQTT_ERR_PACKET_ID_EXHAUSTED;
}

mqtt_error_t mqtt_packet_id_free(mqtt_packet_id_allocator_t *allocator, uint16_t packet_id)
{
    if (!allocator) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Packet ID 0 cannot be freed (reserved) */
    if (packet_id == 0) {
        return MQTT_ERR_INVALID_PACKET_ID;
    }

    /* Check if actually allocated */
    if (!(allocator->bitmap[BITMAP_BYTE(packet_id)] & BITMAP_BIT(packet_id))) {
        return MQTT_ERR_INVALID_PACKET_ID;  /* Was not allocated */
    }

    /* Free the ID */
    allocator->bitmap[BITMAP_BYTE(packet_id)] &= ~BITMAP_BIT(packet_id);
    allocator->allocated_count--;

    return MQTT_OK;
}

bool mqtt_packet_id_is_allocated(const mqtt_packet_id_allocator_t *allocator, uint16_t packet_id)
{
    if (!allocator || packet_id == 0) {
        return false;
    }

    return (allocator->bitmap[BITMAP_BYTE(packet_id)] & BITMAP_BIT(packet_id)) != 0;
}

uint16_t mqtt_packet_id_count(const mqtt_packet_id_allocator_t *allocator)
{
    if (!allocator) {
        return 0;
    }

    return allocator->allocated_count;
}
