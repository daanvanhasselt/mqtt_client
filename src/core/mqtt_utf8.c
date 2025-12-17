/**
 * @file mqtt_utf8.c
 * @brief MQTT UTF-8 String Encoding/Decoding and Validation Implementation
 */

#include "mqtt_utf8.h"
#include <mqtt/mqtt_error.h>
#include <string.h>

/* ========================================================================== */
/* Internal Helper Functions                                                  */
/* ========================================================================== */

/**
 * @brief Check if a Unicode code point is a non-character
 *
 * Non-characters are defined as:
 * - U+FFFE and U+FFFF in each plane (34 total)
 * - U+FDD0 through U+FDEF (32 total)
 */
static bool is_noncharacter(uint32_t codepoint)
{
    /* Check for U+FFFE and U+FFFF in any plane */
    if ((codepoint & 0xFFFE) == 0xFFFE) {
        return true;
    }

    /* Check for U+FDD0 through U+FDEF */
    if (codepoint >= 0xFDD0 && codepoint <= 0xFDEF) {
        return true;
    }

    return false;
}

/* ========================================================================== */
/* Encoding Implementation                                                    */
/* ========================================================================== */

int mqtt_utf8_encode(const char *str, size_t str_len, uint8_t *buf, size_t buf_len)
{
    /* Validate parameters */
    if (buf == NULL) {
        return -1;
    }

    if (str_len > 0 && str == NULL) {
        return -1;
    }

    if (str_len > MQTT_UTF8_MAX_LENGTH) {
        return -1;
    }

    /* Check if buffer has enough space */
    size_t required_size = mqtt_utf8_encoded_size(str_len);
    if (buf_len < required_size) {
        return -1;
    }

    /* Write length prefix (big-endian uint16_t) */
    buf[0] = (uint8_t)((str_len >> 8) & 0xFF);  /* MSB */
    buf[1] = (uint8_t)(str_len & 0xFF);         /* LSB */

    /* Copy string data */
    if (str_len > 0) {
        memcpy(buf + 2, str, str_len);
    }

    return (int)required_size;
}

/* ========================================================================== */
/* Decoding Implementation                                                    */
/* ========================================================================== */

int mqtt_utf8_decode(const uint8_t *buf, size_t buf_len, const char **str_out, uint16_t *len_out)
{
    /* Validate parameters */
    if (buf == NULL || str_out == NULL || len_out == NULL) {
        return -1;
    }

    /* Check if we have enough data for length prefix */
    if (buf_len < MQTT_UTF8_LENGTH_PREFIX_SIZE) {
        return -1;
    }

    /* Read length prefix (big-endian uint16_t) */
    uint16_t str_len = ((uint16_t)buf[0] << 8) | buf[1];

    /* Check if we have enough data for the string */
    size_t required_size = mqtt_utf8_encoded_size(str_len);
    if (buf_len < required_size) {
        return -1;
    }

    /* Set output parameters */
    *str_out = (const char *)(buf + 2);
    *len_out = str_len;

    return (int)required_size;
}

/* ========================================================================== */
/* Validation Implementation                                                  */
/* ========================================================================== */

bool mqtt_utf8_validate(const char *str, size_t len)
{
    size_t i = 0;

    if (str == NULL && len > 0) {
        return false;
    }

    while (i < len) {
        uint8_t byte = (uint8_t)str[i];
        uint32_t codepoint;
        size_t sequence_len;

        /* Determine sequence length from first byte */
        if ((byte & 0x80) == 0x00) {
            /* 1-byte sequence: 0xxxxxxx */
            sequence_len = 1;
            codepoint = byte;
        } else if ((byte & 0xE0) == 0xC0) {
            /* 2-byte sequence: 110xxxxx 10xxxxxx */
            sequence_len = 2;
            codepoint = byte & 0x1F;
        } else if ((byte & 0xF0) == 0xE0) {
            /* 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx */
            sequence_len = 3;
            codepoint = byte & 0x0F;
        } else if ((byte & 0xF8) == 0xF0) {
            /* 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
            sequence_len = 4;
            codepoint = byte & 0x07;
        } else {
            /* Invalid first byte */
            return false;
        }

        /* Check if we have enough bytes for the sequence */
        if (i + sequence_len > len) {
            return false;
        }

        /* Validate continuation bytes and build codepoint */
        for (size_t j = 1; j < sequence_len; j++) {
            byte = (uint8_t)str[i + j];

            /* Continuation bytes must be 10xxxxxx */
            if ((byte & 0xC0) != 0x80) {
                return false;
            }

            /* Add 6 bits to codepoint */
            codepoint = (codepoint << 6) | (byte & 0x3F);
        }

        /* Validate codepoint range and encoding */
        if (sequence_len == 1) {
            /* U+0000 is not allowed in MQTT */
            if (codepoint == 0x0000) {
                return false;
            }
        } else if (sequence_len == 2) {
            /* Must be >= U+0080 (no overlong encoding) */
            if (codepoint < 0x0080) {
                return false;
            }
        } else if (sequence_len == 3) {
            /* Must be >= U+0800 (no overlong encoding) */
            if (codepoint < 0x0800) {
                return false;
            }

            /* UTF-16 surrogates (U+D800 - U+DFFF) are invalid */
            if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
                return false;
            }
        } else if (sequence_len == 4) {
            /* Must be >= U+10000 (no overlong encoding) */
            if (codepoint < 0x10000) {
                return false;
            }

            /* Must be <= U+10FFFF (maximum Unicode codepoint) */
            if (codepoint > 0x10FFFF) {
                return false;
            }
        }

        /* Check for non-characters */
        if (is_noncharacter(codepoint)) {
            return false;
        }

        /* Move to next sequence */
        i += sequence_len;
    }

    return true;
}
