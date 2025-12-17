/**
 * @file mqtt_utf8.h
 * @brief MQTT UTF-8 String Encoding/Decoding and Validation
 *
 * This module implements MQTT UTF-8 string handling as specified in the
 * MQTT specification. MQTT uses a 2-byte length prefix followed by the
 * UTF-8 encoded string data.
 *
 * String format:
 * - Bytes 0-1: Length (MSB first, big-endian uint16_t)
 * - Bytes 2+: UTF-8 encoded string data
 *
 * MQTT UTF-8 restrictions (beyond standard UTF-8):
 * - NULL character (U+0000) is not allowed
 * - Non-characters (U+FFFE, U+FFFF, etc.) are not allowed
 * - Control characters U+0001 through U+001F are not recommended
 * - Control characters U+007F through U+009F are not recommended
 *
 * @note This is an internal header - not part of the public API
 */

#ifndef MQTT_UTF8_H
#define MQTT_UTF8_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

/**
 * @brief Maximum length of an MQTT UTF-8 string
 *
 * Since the length is encoded as a 16-bit value, the maximum
 * string length is 65535 bytes.
 */
#define MQTT_UTF8_MAX_LENGTH  65535u

/**
 * @brief Size of the length prefix (2 bytes)
 */
#define MQTT_UTF8_LENGTH_PREFIX_SIZE  2

/* ========================================================================== */
/* Encoding Functions                                                         */
/* ========================================================================== */

/**
 * @brief Encode a UTF-8 string with MQTT 2-byte length prefix
 *
 * Encodes the given string as an MQTT UTF-8 string by prefixing it with
 * a 2-byte length field (big-endian). The string is NOT null-terminated
 * in the encoded form.
 *
 * @param str String to encode (UTF-8 encoded, may not be null-terminated)
 * @param str_len Length of string in bytes
 * @param buf Buffer to write encoded string (must have str_len + 2 bytes available)
 * @param buf_len Size of buffer in bytes
 * @return Number of bytes written (str_len + 2), or -1 on error
 *
 * Error conditions:
 * - str_len > MQTT_UTF8_MAX_LENGTH (65535)
 * - buf_len < (str_len + 2)
 * - str is NULL (when str_len > 0)
 * - buf is NULL
 *
 * @note The string is NOT validated for UTF-8 correctness by this function
 * @note Use mqtt_utf8_validate() to validate the string before encoding
 *
 * @example
 * @code
 * const char *topic = "home/temperature";
 * uint8_t buf[100];
 * int len = mqtt_utf8_encode(topic, strlen(topic), buf, sizeof(buf));
 * // len = 18 (2 bytes length + 16 bytes data)
 * // buf = {0x00, 0x10, 'h', 'o', 'm', 'e', '/', ...}
 * @endcode
 */
int mqtt_utf8_encode(const char *str, size_t str_len, uint8_t *buf, size_t buf_len);

/**
 * @brief Decode an MQTT UTF-8 string
 *
 * Decodes an MQTT UTF-8 string by reading the 2-byte length prefix and
 * setting the output pointer to the string data within the buffer.
 *
 * @param buf Buffer containing encoded string (length prefix + data)
 * @param buf_len Size of buffer in bytes
 * @param str_out Output pointer to string data (points within buf, NOT null-terminated)
 * @param len_out Output length of string in bytes
 * @return Number of bytes consumed (len_out + 2), or -1 on error
 *
 * Error conditions:
 * - buf_len < 2 (not enough data for length prefix)
 * - buf_len < (length + 2) (incomplete string data)
 * - buf, str_out, or len_out is NULL
 *
 * @note The returned string pointer is NOT null-terminated
 * @note The string pointer points directly into the input buffer
 * @note The string is NOT validated for UTF-8 correctness by this function
 * @note Use mqtt_utf8_validate() to validate the decoded string
 *
 * @example
 * @code
 * uint8_t data[] = {0x00, 0x04, 't', 'e', 's', 't'};
 * const char *str;
 * uint16_t len;
 * int consumed = mqtt_utf8_decode(data, sizeof(data), &str, &len);
 * // consumed = 6, str points to "test" (within data), len = 4
 * @endcode
 */
int mqtt_utf8_decode(const uint8_t *buf, size_t buf_len, const char **str_out, uint16_t *len_out);

/* ========================================================================== */
/* Validation Functions                                                       */
/* ========================================================================== */

/**
 * @brief Validate a UTF-8 string according to MQTT specification
 *
 * Validates that the given string is:
 * - Valid UTF-8 encoding
 * - Does not contain NULL character (U+0000)
 * - Does not contain non-characters (U+FFFE, U+FFFF, etc.)
 *
 * @param str String to validate (UTF-8 encoded)
 * @param len Length of string in bytes
 * @return true if valid, false if invalid
 *
 * @note This function performs strict validation according to MQTT spec
 * @note Control characters (U+0001-U+001F, U+007F-U+009F) are allowed
 *       but not recommended
 *
 * @example
 * @code
 * const char *topic = "home/temperature";
 * if (mqtt_utf8_validate(topic, strlen(topic))) {
 *     // String is valid for MQTT
 * }
 * @endcode
 */
bool mqtt_utf8_validate(const char *str, size_t len);

/* ========================================================================== */
/* Helper Functions                                                           */
/* ========================================================================== */

/**
 * @brief Get encoded size of a string (2 + len)
 *
 * Calculates the total size needed to encode a string of the given
 * length, including the 2-byte length prefix.
 *
 * @param len String length in bytes
 * @return Encoded size (len + 2)
 *
 * @note This is a simple inline helper function
 * @note Does not validate that len <= MQTT_UTF8_MAX_LENGTH
 */
static inline size_t mqtt_utf8_encoded_size(size_t len)
{
    return MQTT_UTF8_LENGTH_PREFIX_SIZE + len;
}

#ifdef __cplusplus
}
#endif

#endif /* MQTT_UTF8_H */
