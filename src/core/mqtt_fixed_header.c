/**
 * @file mqtt_fixed_header.c
 * @brief MQTT Fixed Header Encoding/Decoding Implementation
 *
 * This module implements encoding and decoding of MQTT fixed headers.
 * The fixed header is present in all MQTT control packets and consists of:
 * - Byte 1: Packet type (bits 7-4) and flags (bits 3-0)
 * - Bytes 2+: Remaining length (variable-length encoded, 1-4 bytes)
 */

#include "mqtt_packet.h"
#include "mqtt_varint.h"
#include <mqtt/mqtt_error.h>

/* ========================================================================== */
/* Encoding Implementation                                                    */
/* ========================================================================== */

/**
 * @brief Encode fixed header
 *
 * Encodes an MQTT fixed header into the provided buffer. The fixed header
 * consists of one byte (packet type + flags) followed by the variable-length
 * encoded remaining length field (1-4 bytes).
 *
 * @param header Pointer to fixed header structure to encode
 * @param buf Buffer to write encoded header (must have at least 5 bytes available)
 * @param buf_len Size of buffer in bytes
 * @return Number of bytes written (1 + varint size), or -1 on error
 *
 * Error conditions:
 * - header or buf is NULL
 * - header->remaining_len > MQTT_MAX_REMAINING_LENGTH
 * - buf_len < required size (1 + varint size)
 *
 * @note Minimum buffer size should be 5 bytes (1 + 4 for max varint)
 */
int mqtt_fixed_header_encode(const mqtt_fixed_header_t *header, uint8_t *buf, size_t buf_len)
{
    /* Validate parameters */
    if (header == NULL || buf == NULL) {
        return -1;
    }

    if (header->remaining_len > MQTT_MAX_REMAINING_LENGTH) {
        return -1;
    }

    /* Calculate required buffer size */
    int varint_size = mqtt_varint_size(header->remaining_len);
    if (varint_size < 0) {
        return -1;
    }

    size_t required_size = 1 + (size_t)varint_size;
    if (buf_len < required_size) {
        return -1;
    }

    /* Encode first byte: packet type (bits 7-4) + flags (bits 3-0) */
    buf[0] = MQTT_MAKE_FIXED_BYTE(header->type, header->flags);

    /* Encode remaining length as variable-length integer */
    int varint_written = mqtt_varint_encode(header->remaining_len, buf + 1);
    if (varint_written < 0) {
        return -1;
    }

    return 1 + varint_written;
}

/* ========================================================================== */
/* Decoding Implementation                                                    */
/* ========================================================================== */

/**
 * @brief Decode fixed header
 *
 * Decodes an MQTT fixed header from the provided buffer. The function reads
 * the first byte (packet type + flags) and then decodes the variable-length
 * remaining length field.
 *
 * @param buf Buffer containing encoded fixed header
 * @param buf_len Number of available bytes in buffer
 * @param header Output parameter for decoded header
 * @return Number of bytes consumed (1 + varint size), 0 if need more data, or -1 on error
 *
 * Return values:
 * - Positive: Number of bytes consumed (successful decode)
 * - 0: Need more data (incomplete variable-length integer)
 * - -1: Malformed packet or invalid data
 *
 * Error conditions:
 * - buf or header is NULL
 * - buf_len is 0
 * - Malformed variable-length integer
 * - remaining_len > MQTT_MAX_REMAINING_LENGTH
 *
 * @note Returns 0 if the variable-length integer is incomplete (need more data)
 * @note The caller should retry with more data when 0 is returned
 */
int mqtt_fixed_header_decode(const uint8_t *buf, size_t buf_len, mqtt_fixed_header_t *header)
{
    /* Validate parameters */
    if (buf == NULL || header == NULL) {
        return -1;
    }

    if (buf_len < 1) {
        /* Need at least 1 byte for first byte */
        return 0;
    }

    /* Decode first byte: packet type (bits 7-4) + flags (bits 3-0) */
    uint8_t first_byte = buf[0];
    header->type = MQTT_PACKET_TYPE(first_byte);
    header->flags = MQTT_PACKET_FLAGS(first_byte);

    /* Validate packet type */
    if (header->type < MQTT_PACKET_CONNECT || header->type > MQTT_PACKET_AUTH) {
        /* Invalid packet type */
        return -1;
    }

    /* Decode remaining length (variable-length integer) */
    uint32_t remaining_len;
    int varint_consumed = mqtt_varint_decode(buf + 1, buf_len - 1, &remaining_len);

    if (varint_consumed < 0) {
        /* Malformed or incomplete varint */
        /* Check if it's incomplete (need more data) or malformed */
        /* For now, treat all errors as "need more data" unless we have enough bytes */
        if (buf_len < 5) {
            /* Could be incomplete - need more data */
            return 0;
        }
        /* We have enough bytes for a complete varint, so it must be malformed */
        return -1;
    }

    /* Validate remaining length */
    if (remaining_len > MQTT_MAX_REMAINING_LENGTH) {
        return -1;
    }

    header->remaining_len = remaining_len;
    return 1 + varint_consumed;
}

/* ========================================================================== */
/* Size Calculation                                                           */
/* ========================================================================== */

/**
 * @brief Get the size needed to encode a fixed header
 *
 * Calculates the total size needed to encode a fixed header with the
 * given remaining length value. This is useful for pre-allocating buffers.
 *
 * @param remaining_len Remaining length value
 * @return Size in bytes (1 + varint size), or -1 if remaining_len is too large
 *
 * @note Returns -1 if remaining_len > MQTT_MAX_REMAINING_LENGTH
 */
int mqtt_fixed_header_size(uint32_t remaining_len)
{
    /* Validate remaining length */
    if (remaining_len > MQTT_MAX_REMAINING_LENGTH) {
        return -1;
    }

    /* Calculate varint size */
    int varint_size = mqtt_varint_size(remaining_len);
    if (varint_size < 0) {
        return -1;
    }

    return 1 + varint_size;
}
