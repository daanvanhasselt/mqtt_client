/**
 * @file mqtt_v311_parser.c
 * @brief MQTT 3.1.1 Packet Decoding Implementation
 *
 * This file implements all packet decoding functions for MQTT 3.1.1 protocol.
 * Each function parses a specific packet type according to the MQTT 3.1.1
 * specification.
 */

#include "mqtt_v311.h"
#include "../../core/mqtt_packet.h"
#include "../../core/mqtt_utf8.h"
#include "../../memory/mqtt_memory.h"
#include <string.h>

/* ========================================================================== */
/* Internal Helper Functions                                                  */
/* ========================================================================== */

/**
 * @brief Decode 16-bit integer from big-endian format
 *
 * @param buf Input buffer
 * @return Decoded value
 */
static uint16_t decode_uint16(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | buf[1];
}

/* ========================================================================== */
/* CONNACK Packet Decoding                                                    */
/* ========================================================================== */

mqtt_error_t mqtt_v311_decode_connack(const uint8_t *buf, size_t len, mqtt_v311_connack_t *out)
{
    if (!buf || !out) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* CONNACK variable header is exactly 2 bytes */
    if (len != 2) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    /* Byte 1: Connect acknowledge flags */
    uint8_t ack_flags = buf[0];
    out->session_present = (ack_flags & 0x01) != 0;

    /* Bits 7-1 are reserved and must be 0 */
    if (ack_flags & 0xFE) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    /* Byte 2: Connect return code */
    out->return_code = buf[1];

    /* Validate return code (0-5 are valid for MQTT 3.1.1) */
    if (out->return_code > 5) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    return MQTT_OK;
}

/* ========================================================================== */
/* PUBLISH Packet Decoding                                                    */
/* ========================================================================== */

mqtt_error_t mqtt_v311_decode_publish(const uint8_t *buf, size_t len, uint8_t flags, mqtt_v311_publish_t *out)
{
    if (!buf || !out || len == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    size_t offset = 0;

    /* Extract flags from fixed header */
    out->dup = MQTT_PUBLISH_DUP(flags) != 0;
    out->qos = (mqtt_qos_t)MQTT_PUBLISH_QOS(flags);
    out->retain = MQTT_PUBLISH_RETAIN(flags) != 0;

    /* Validate QoS */
    if (out->qos > MQTT_QOS_2) {
        return MQTT_ERR_INVALID_QOS;
    }

    /* Decode topic name */
    int consumed = mqtt_utf8_decode(buf + offset, len - offset, &out->topic, &out->topic_len);
    if (consumed < 0) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    /* Validate topic (no wildcards allowed in PUBLISH) */
    for (uint16_t i = 0; i < out->topic_len; i++) {
        if (out->topic[i] == '+' || out->topic[i] == '#') {
            return MQTT_ERR_INVALID_TOPIC;
        }
    }

    /* Validate UTF-8 encoding */
    if (!mqtt_utf8_validate(out->topic, out->topic_len)) {
        return MQTT_ERR_INVALID_UTF8;
    }

    offset += (size_t)consumed;

    /* Decode packet ID (only for QoS > 0) */
    if (out->qos > MQTT_QOS_0) {
        if (len - offset < 2) {
            return MQTT_ERR_MALFORMED_PACKET;
        }
        out->packet_id = decode_uint16(buf + offset);
        if (out->packet_id == 0) {
            return MQTT_ERR_INVALID_PACKET_ID;
        }
        offset += 2;
    } else {
        out->packet_id = 0;
    }

    /* Remaining data is the payload */
    out->payload = buf + offset;
    out->payload_len = len - offset;

    return MQTT_OK;
}

/* ========================================================================== */
/* Acknowledgment Packet Decoding (PUBACK/PUBREC/PUBREL/PUBCOMP/UNSUBACK)  */
/* ========================================================================== */

mqtt_error_t mqtt_v311_decode_ack(const uint8_t *buf, size_t len, uint16_t *packet_id)
{
    if (!buf || !packet_id) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* All acknowledgment packets have exactly 2 bytes in variable header */
    if (len != 2) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    *packet_id = decode_uint16(buf);

    /* Packet ID must be non-zero */
    if (*packet_id == 0) {
        return MQTT_ERR_INVALID_PACKET_ID;
    }

    return MQTT_OK;
}

/* ========================================================================== */
/* SUBACK Packet Decoding                                                    */
/* ========================================================================== */

mqtt_error_t mqtt_v311_decode_suback(const uint8_t *buf, size_t len, mqtt_v311_suback_t *out)
{
    if (!buf || !out || len < 3) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Decode packet ID */
    out->packet_id = decode_uint16(buf);
    if (out->packet_id == 0) {
        return MQTT_ERR_INVALID_PACKET_ID;
    }

    /* Remaining bytes are return codes */
    out->count = len - 2;
    out->return_codes = (uint8_t *)mqtt_malloc(out->count);
    if (!out->return_codes) {
        return MQTT_ERR_NOMEM;
    }

    /* Copy and validate return codes */
    for (size_t i = 0; i < out->count; i++) {
        uint8_t code = buf[2 + i];
        /* Valid codes: 0x00, 0x01, 0x02 (granted QoS) or 0x80 (failure) */
        if (code > 2 && code != 0x80) {
            mqtt_free(out->return_codes);
            out->return_codes = NULL;
            return MQTT_ERR_MALFORMED_PACKET;
        }
        out->return_codes[i] = code;
    }

    return MQTT_OK;
}

/* ========================================================================== */
/* UNSUBACK Packet Decoding                                                  */
/* ========================================================================== */

mqtt_error_t mqtt_v311_decode_unsuback(const uint8_t *buf, size_t len, uint16_t *packet_id)
{
    /* UNSUBACK has the same format as other acknowledgment packets */
    return mqtt_v311_decode_ack(buf, len, packet_id);
}
