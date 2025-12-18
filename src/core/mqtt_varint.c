/**
 * @file mqtt_varint.c
 * @brief Variable-Length Integer Encoding/Decoding Implementation
 *
 * Optimized for common cases: most MQTT packets have small remaining lengths
 * (1-2 bytes), so the encode/decode paths are optimized for these cases.
 */

#include "mqtt_varint.h"
#include <mqtt/mqtt_error.h>
#include "mqtt_compiler.h"

/* ========================================================================== */
/* Encoding Implementation                                                    */
/* ========================================================================== */

MQTT_HOT
int mqtt_varint_encode(uint32_t value, uint8_t *MQTT_RESTRICT buf)
{
    /* Fast path for small values (most common case in MQTT) */
    if (MQTT_LIKELY(value < 128)) {
        buf[0] = (uint8_t)value;
        return 1;
    }

    /* Second most common: 2-byte encoding (up to 16383) */
    if (MQTT_LIKELY(value < 16384)) {
        buf[0] = (uint8_t)((value & 0x7F) | 0x80);
        buf[1] = (uint8_t)(value >> 7);
        return 2;
    }

    /* Less common: 3-byte encoding (up to 2097151) */
    if (value < 2097152) {
        buf[0] = (uint8_t)((value & 0x7F) | 0x80);
        buf[1] = (uint8_t)(((value >> 7) & 0x7F) | 0x80);
        buf[2] = (uint8_t)(value >> 14);
        return 3;
    }

    /* Rare: 4-byte encoding */
    if (MQTT_LIKELY(value <= MQTT_VARINT_MAX_VALUE)) {
        buf[0] = (uint8_t)((value & 0x7F) | 0x80);
        buf[1] = (uint8_t)(((value >> 7) & 0x7F) | 0x80);
        buf[2] = (uint8_t)(((value >> 14) & 0x7F) | 0x80);
        buf[3] = (uint8_t)(value >> 21);
        return 4;
    }

    /* Value too large */
    return -1;
}

/* ========================================================================== */
/* Decoding Implementation                                                    */
/* ========================================================================== */

MQTT_HOT
int mqtt_varint_decode(const uint8_t *MQTT_RESTRICT buf, size_t max_len,
                       uint32_t *MQTT_RESTRICT value)
{
    uint8_t byte0;

    /* Need at least 1 byte */
    if (MQTT_UNLIKELY(max_len == 0)) {
        return -1;
    }

    /* Fast path for 1-byte values (most common) */
    byte0 = buf[0];
    if (MQTT_LIKELY((byte0 & 0x80) == 0)) {
        *value = byte0;
        return 1;
    }

    /* Need at least 2 bytes */
    if (MQTT_UNLIKELY(max_len < 2)) {
        return -1;
    }

    uint8_t byte1 = buf[1];
    if (MQTT_LIKELY((byte1 & 0x80) == 0)) {
        *value = (byte0 & 0x7F) | ((uint32_t)byte1 << 7);
        return 2;
    }

    /* Need at least 3 bytes */
    if (MQTT_UNLIKELY(max_len < 3)) {
        return -1;
    }

    uint8_t byte2 = buf[2];
    if ((byte2 & 0x80) == 0) {
        *value = (byte0 & 0x7F) | ((uint32_t)(byte1 & 0x7F) << 7) |
                 ((uint32_t)byte2 << 14);
        return 3;
    }

    /* Need exactly 4 bytes */
    if (MQTT_UNLIKELY(max_len < 4)) {
        return -1;
    }

    uint8_t byte3 = buf[3];

    /* 4-byte encoding: byte3 must NOT have continuation bit set */
    if (MQTT_UNLIKELY(byte3 & 0x80)) {
        return -1;  /* Malformed: more than 4 bytes */
    }

    uint32_t result = (byte0 & 0x7F) | ((uint32_t)(byte1 & 0x7F) << 7) |
                      ((uint32_t)(byte2 & 0x7F) << 14) | ((uint32_t)byte3 << 21);

    /* Check for overflow */
    if (MQTT_UNLIKELY(result > MQTT_VARINT_MAX_VALUE)) {
        return -1;
    }

    *value = result;
    return 4;
}

/* ========================================================================== */
/* Size Calculation                                                           */
/* ========================================================================== */

MQTT_CONST
int mqtt_varint_size(uint32_t value)
{
    /* Most common case first for better branch prediction */
    if (MQTT_LIKELY(value < 128)) {
        return 1;
    }
    if (MQTT_LIKELY(value < 16384)) {        /* 128 * 128 */
        return 2;
    }
    if (value < 2097152) {                    /* 128 * 128 * 128 */
        return 3;
    }
    if (MQTT_LIKELY(value <= MQTT_VARINT_MAX_VALUE)) {
        return 4;
    }
    return -1;  /* Value too large */
}
