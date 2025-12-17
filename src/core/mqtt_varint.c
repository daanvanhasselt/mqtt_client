/**
 * @file mqtt_varint.c
 * @brief Variable-Length Integer Encoding/Decoding Implementation
 */

#include "mqtt_varint.h"
#include <mqtt/mqtt_error.h>

/* ========================================================================== */
/* Encoding Implementation                                                    */
/* ========================================================================== */

int mqtt_varint_encode(uint32_t value, uint8_t *buf)
{
    int bytes_written = 0;

    /* Validate value is within range */
    if (value > MQTT_VARINT_MAX_VALUE) {
        return -1;
    }

    /* Encode 7 bits at a time, LSB first */
    do {
        uint8_t encoded_byte = value % 128;  /* Lower 7 bits */
        value = value / 128;

        /* Set continuation bit if more bytes follow */
        if (value > 0) {
            encoded_byte |= 0x80;  /* Set bit 7 */
        }

        buf[bytes_written++] = encoded_byte;
    } while (value > 0);

    return bytes_written;
}

/* ========================================================================== */
/* Decoding Implementation                                                    */
/* ========================================================================== */

int mqtt_varint_decode(const uint8_t *buf, size_t max_len, uint32_t *value)
{
    uint32_t decoded_value = 0;
    uint32_t multiplier = 1;
    int bytes_consumed = 0;
    uint8_t current_byte;

    /* Decode up to 4 bytes */
    do {
        /* Check if we have data available */
        if (bytes_consumed >= (int)max_len) {
            /* Incomplete - need more data */
            return -1;
        }

        /* Check if we've exceeded maximum length */
        if (bytes_consumed >= MQTT_VARINT_MAX_BYTES) {
            /* Malformed - more than 4 bytes */
            return -1;
        }

        current_byte = buf[bytes_consumed++];

        /* Accumulate value from lower 7 bits */
        decoded_value += (current_byte & 0x7F) * multiplier;

        /* Check for overflow */
        if (decoded_value > MQTT_VARINT_MAX_VALUE) {
            /* Value too large */
            return -1;
        }

        multiplier *= 128;

        /* If multiplier overflows, encoding is malformed */
        if (multiplier > (MQTT_VARINT_MAX_VALUE + 1)) {
            return -1;
        }

    } while ((current_byte & 0x80) != 0);  /* Continue while bit 7 is set */

    *value = decoded_value;
    return bytes_consumed;
}

/* ========================================================================== */
/* Size Calculation                                                           */
/* ========================================================================== */

int mqtt_varint_size(uint32_t value)
{
    /* Validate value is within range */
    if (value > MQTT_VARINT_MAX_VALUE) {
        return -1;
    }

    /* Calculate number of bytes needed */
    if (value < 128) {
        return 1;
    } else if (value < 16384) {          /* 128 * 128 */
        return 2;
    } else if (value < 2097152) {        /* 128 * 128 * 128 */
        return 3;
    } else {
        return 4;
    }
}
