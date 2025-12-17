/**
 * @file mqtt_varint.h
 * @brief Variable-Length Integer Encoding/Decoding
 *
 * This module implements MQTT variable-length integer encoding as specified
 * in the MQTT specification. Variable-length integers are used to encode the
 * "Remaining Length" field in the fixed header.
 *
 * Encoding scheme:
 * - Each byte encodes 7 bits of data and 1 continuation bit
 * - Bit 7 (MSB) is the continuation bit (1 = more bytes follow, 0 = last byte)
 * - Bits 6-0 contain the data
 * - Least significant byte is transmitted first
 * - Maximum value is 268,435,455 (0x0FFFFFFF) using 4 bytes
 *
 * @note This is an internal header - not part of the public API
 */

#ifndef MQTT_VARINT_H
#define MQTT_VARINT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

/**
 * @brief Maximum value that can be encoded in a variable-length integer
 */
#define MQTT_VARINT_MAX_VALUE  268435455u  /* 0x0FFFFFFF */

/**
 * @brief Maximum number of bytes in a variable-length integer
 */
#define MQTT_VARINT_MAX_BYTES  4

/* ========================================================================== */
/* Encoding Functions                                                         */
/* ========================================================================== */

/**
 * @brief Encode a variable-length integer
 *
 * Encodes the given value as a variable-length integer according to the
 * MQTT specification. The least significant byte is written first.
 *
 * @param value Value to encode (must be <= MQTT_VARINT_MAX_VALUE)
 * @param buf Buffer to write encoded bytes (must have at least 4 bytes available)
 * @return Number of bytes written (1-4), or -1 on error (value too large)
 *
 * @note The caller must ensure buf has at least MQTT_VARINT_MAX_BYTES (4) bytes
 * @note Returns -1 if value > MQTT_VARINT_MAX_VALUE (268,435,455)
 *
 * @example
 * @code
 * uint8_t buf[4];
 * int len = mqtt_varint_encode(16384, buf);
 * // len = 3, buf = {0x80, 0x80, 0x01}
 * @endcode
 */
int mqtt_varint_encode(uint32_t value, uint8_t *buf);

/**
 * @brief Decode a variable-length integer
 *
 * Decodes a variable-length integer from the given buffer. The function
 * reads bytes until it encounters a byte with bit 7 cleared (continuation
 * bit = 0), or until 4 bytes have been read.
 *
 * @param buf Buffer containing encoded variable-length integer
 * @param max_len Number of available bytes in buf
 * @param value Output parameter for decoded value
 * @return Number of bytes consumed (1-4), or -1 on error
 *
 * Error conditions:
 * - Malformed encoding (more than 4 bytes)
 * - Incomplete encoding (need more data)
 * - Value exceeds MQTT_VARINT_MAX_VALUE
 *
 * @note Sets *value only on success
 * @note Returns -1 if encoding is malformed or incomplete
 *
 * @example
 * @code
 * uint8_t data[] = {0x80, 0x80, 0x01};
 * uint32_t value;
 * int consumed = mqtt_varint_decode(data, sizeof(data), &value);
 * // consumed = 3, value = 16384
 * @endcode
 */
int mqtt_varint_decode(const uint8_t *buf, size_t max_len, uint32_t *value);

/**
 * @brief Get the encoded size of a value
 *
 * Calculates how many bytes are needed to encode the given value
 * as a variable-length integer, without actually encoding it.
 *
 * @param value Value to check (must be <= MQTT_VARINT_MAX_VALUE)
 * @return Number of bytes needed (1-4), or -1 if value is too large
 *
 * @note Returns -1 if value > MQTT_VARINT_MAX_VALUE (268,435,455)
 *
 * @example
 * @code
 * int size = mqtt_varint_size(127);   // Returns 1
 * size = mqtt_varint_size(128);       // Returns 2
 * size = mqtt_varint_size(16384);     // Returns 3
 * size = mqtt_varint_size(2097152);   // Returns 4
 * @endcode
 */
int mqtt_varint_size(uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_VARINT_H */
