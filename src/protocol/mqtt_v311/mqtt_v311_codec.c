/**
 * @file mqtt_v311_codec.c
 * @brief MQTT 3.1.1 Packet Encoding Implementation
 *
 * This file implements all packet encoding functions for MQTT 3.1.1 protocol.
 * Each function serializes a specific packet type according to the MQTT 3.1.1
 * specification.
 */

#include "mqtt_v311.h"
#include "../../core/mqtt_packet.h"
#include "../../core/mqtt_varint.h"
#include "../../core/mqtt_utf8.h"
#include "../../memory/mqtt_memory.h"
#include <string.h>

/* ========================================================================== */
/* Internal Helper Functions                                                  */
/* ========================================================================== */

/**
 * @brief Encode 16-bit integer in big-endian format
 *
 * @param buf Output buffer
 * @param value Value to encode
 * @return Number of bytes written (always 2)
 */
static size_t encode_uint16(uint8_t *buf, uint16_t value)
{
    buf[0] = (value >> 8) & 0xFF;
    buf[1] = value & 0xFF;
    return 2;
}

/**
 * @brief Calculate size of encoded string (length prefix + data)
 *
 * @param str String (NULL-terminated)
 * @return Total size in bytes
 */
static size_t string_encoded_size(const char *str)
{
    return mqtt_utf8_encoded_size(str ? strlen(str) : 0);
}

/* ========================================================================== */
/* CONNECT Packet Encoding                                                    */
/* ========================================================================== */

ssize_t mqtt_v311_encode_connect(mqtt_buffer_t *buf, const mqtt_connect_opts_t *opts)
{
    if (!buf || !opts) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Validate required fields */
    if (!opts->client_id) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Calculate remaining length */
    uint32_t remaining_len = 0;

    /* Variable header: Protocol name (6) + Protocol level (1) + Flags (1) + Keep-alive (2) */
    remaining_len += 10;

    /* Payload: Client ID */
    remaining_len += string_encoded_size(opts->client_id);

    /* Will topic and message */
    if (opts->will) {
        remaining_len += string_encoded_size(opts->will->topic);
        remaining_len += 2 + opts->will->payload_len;  /* Length prefix + payload */
    }

    /* Username */
    if (opts->username) {
        remaining_len += string_encoded_size(opts->username);
    }

    /* Password */
    if (opts->password) {
        remaining_len += 2 + (opts->password_len > 0 ? opts->password_len : strlen((const char *)opts->password));
    }

    /* Check maximum packet size */
    if (remaining_len > MQTT_MAX_REMAINING_LENGTH) {
        return MQTT_ERR_PACKET_TOO_LARGE;
    }

    /* Allocate temporary buffer for encoding */
    uint8_t temp[8192];  /* Should be sufficient for most CONNECT packets */
    uint8_t *ptr = temp;
    size_t offset = 0;

    /* Fixed header */
    ptr[offset++] = MQTT_MAKE_FIXED_BYTE(MQTT_PACKET_CONNECT, 0);
    offset += mqtt_varint_encode(remaining_len, ptr + offset);

    /* Variable header - Protocol name */
    offset += mqtt_utf8_encode("MQTT", 4, ptr + offset, sizeof(temp) - offset);

    /* Protocol level (4 for MQTT 3.1.1) */
    ptr[offset++] = 4;

    /* Connect flags */
    uint8_t flags = 0;
    if (opts->username) flags |= 0x80;
    if (opts->password) flags |= 0x40;
    if (opts->will) {
        flags |= 0x04;  /* Will flag */
        flags |= (opts->will->qos & 0x03) << 3;  /* Will QoS */
        if (opts->will->retain) flags |= 0x20;  /* Will retain */
    }
    if (opts->clean_session) flags |= 0x02;
    ptr[offset++] = flags;

    /* Keep-alive */
    offset += encode_uint16(ptr + offset, opts->keepalive_sec);

    /* Payload - Client ID */
    size_t client_id_len = strlen(opts->client_id);
    offset += mqtt_utf8_encode(opts->client_id, client_id_len, ptr + offset, sizeof(temp) - offset);

    /* Will topic and message */
    if (opts->will) {
        size_t will_topic_len = strlen(opts->will->topic);
        offset += mqtt_utf8_encode(opts->will->topic, will_topic_len, ptr + offset, sizeof(temp) - offset);
        uint16_t will_len = (uint16_t)opts->will->payload_len;
        offset += encode_uint16(ptr + offset, will_len);
        if (will_len > 0 && opts->will->payload) {
            memcpy(ptr + offset, opts->will->payload, will_len);
            offset += will_len;
        }
    }

    /* Username */
    if (opts->username) {
        size_t username_len = strlen(opts->username);
        offset += mqtt_utf8_encode(opts->username, username_len, ptr + offset, sizeof(temp) - offset);
    }

    /* Password */
    if (opts->password) {
        uint16_t pass_len = opts->password_len > 0 ? opts->password_len : (uint16_t)strlen((const char *)opts->password);
        offset += encode_uint16(ptr + offset, pass_len);
        if (pass_len > 0) {
            memcpy(ptr + offset, opts->password, pass_len);
            offset += pass_len;
        }
    }

    /* Append to output buffer */
    mqtt_error_t err = mqtt_buffer_append(buf, temp, offset);
    if (err != MQTT_OK) {
        return err;
    }

    return (ssize_t)offset;
}

/* ========================================================================== */
/* PUBLISH Packet Encoding                                                    */
/* ========================================================================== */

ssize_t mqtt_v311_encode_publish(mqtt_buffer_t *buf, const mqtt_publish_opts_t *opts, uint16_t packet_id)
{
    if (!buf || !opts || !opts->topic) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Validate QoS */
    if (opts->qos > MQTT_QOS_2) {
        return MQTT_ERR_INVALID_QOS;
    }

    /* Calculate remaining length */
    uint32_t remaining_len = 0;

    /* Topic name */
    remaining_len += string_encoded_size(opts->topic);

    /* Packet ID (only for QoS > 0) */
    if (opts->qos > MQTT_QOS_0) {
        remaining_len += 2;
    }

    /* Payload */
    remaining_len += opts->payload_len;

    /* Check maximum packet size */
    if (remaining_len > MQTT_MAX_REMAINING_LENGTH) {
        return MQTT_ERR_PACKET_TOO_LARGE;
    }

    /* Fixed header */
    uint8_t flags = MQTT_MAKE_PUBLISH_FLAGS(opts->dup ? 1 : 0, opts->qos, opts->retain ? 1 : 0);
    uint8_t fixed_header[6];
    size_t header_len = 0;
    fixed_header[header_len++] = MQTT_MAKE_FIXED_BYTE(MQTT_PACKET_PUBLISH, flags);
    header_len += mqtt_varint_encode(remaining_len, fixed_header + header_len);

    /* Append fixed header */
    mqtt_error_t err = mqtt_buffer_append(buf, fixed_header, header_len);
    if (err != MQTT_OK) {
        return err;
    }

    /* Variable header - Topic name */
    uint8_t topic_buf[2 + 65535];  /* Max topic length */
    size_t topic_str_len = strlen(opts->topic);
    int topic_len = mqtt_utf8_encode(opts->topic, topic_str_len, topic_buf, sizeof(topic_buf));
    if (topic_len < 0) {
        return MQTT_ERR_INVALID_ARG;
    }
    err = mqtt_buffer_append(buf, topic_buf, topic_len);
    if (err != MQTT_OK) {
        return err;
    }

    /* Packet ID (only for QoS > 0) */
    if (opts->qos > MQTT_QOS_0) {
        uint8_t pid_buf[2];
        encode_uint16(pid_buf, packet_id);
        err = mqtt_buffer_append(buf, pid_buf, 2);
        if (err != MQTT_OK) {
            return err;
        }
    }

    /* Payload */
    if (opts->payload_len > 0 && opts->payload) {
        err = mqtt_buffer_append(buf, opts->payload, opts->payload_len);
        if (err != MQTT_OK) {
            return err;
        }
    }

    return (ssize_t)(header_len + topic_len + (opts->qos > MQTT_QOS_0 ? 2 : 0) + opts->payload_len);
}

/* ========================================================================== */
/* Acknowledgment Packet Encoding (PUBACK/PUBREC/PUBREL/PUBCOMP)            */
/* ========================================================================== */

/**
 * @brief Internal helper to encode simple acknowledgment packets
 *
 * All QoS acknowledgment packets have the same structure:
 * Fixed header + 2-byte packet ID.
 *
 * @param buf Output buffer
 * @param packet_type Packet type (PUBACK, PUBREC, PUBREL, or PUBCOMP)
 * @param flags Reserved flags (0 for most, 0x02 for PUBREL)
 * @param packet_id Packet identifier
 * @return Number of bytes written on success, negative error code on failure
 */
static ssize_t encode_ack_packet(mqtt_buffer_t *buf, mqtt_packet_type_t packet_type,
                                 uint8_t flags, uint16_t packet_id)
{
    if (!buf || packet_id == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    uint8_t packet[4];
    size_t len = 0;

    /* Fixed header */
    packet[len++] = MQTT_MAKE_FIXED_BYTE(packet_type, flags);
    packet[len++] = 2;  /* Remaining length is always 2 */

    /* Packet ID */
    len += encode_uint16(packet + len, packet_id);

    mqtt_error_t err = mqtt_buffer_append(buf, packet, len);
    if (err != MQTT_OK) {
        return err;
    }

    return (ssize_t)len;
}

ssize_t mqtt_v311_encode_puback(mqtt_buffer_t *buf, uint16_t packet_id)
{
    return encode_ack_packet(buf, MQTT_PACKET_PUBACK, 0, packet_id);
}

ssize_t mqtt_v311_encode_pubrec(mqtt_buffer_t *buf, uint16_t packet_id)
{
    return encode_ack_packet(buf, MQTT_PACKET_PUBREC, 0, packet_id);
}

ssize_t mqtt_v311_encode_pubrel(mqtt_buffer_t *buf, uint16_t packet_id)
{
    return encode_ack_packet(buf, MQTT_PACKET_PUBREL, MQTT_FLAGS_PUBREL, packet_id);
}

ssize_t mqtt_v311_encode_pubcomp(mqtt_buffer_t *buf, uint16_t packet_id)
{
    return encode_ack_packet(buf, MQTT_PACKET_PUBCOMP, 0, packet_id);
}

/* ========================================================================== */
/* SUBSCRIBE Packet Encoding                                                 */
/* ========================================================================== */

ssize_t mqtt_v311_encode_subscribe(mqtt_buffer_t *buf, uint16_t packet_id,
                                   const mqtt_v311_subscription_t *subs, size_t count)
{
    if (!buf || !subs || count == 0 || packet_id == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Calculate remaining length */
    uint32_t remaining_len = 2;  /* Packet ID */

    for (size_t i = 0; i < count; i++) {
        if (!subs[i].topic_filter) {
            return MQTT_ERR_INVALID_ARG;
        }
        if (subs[i].qos > MQTT_QOS_2) {
            return MQTT_ERR_INVALID_QOS;
        }
        remaining_len += string_encoded_size(subs[i].topic_filter);
        remaining_len += 1;  /* QoS byte */
    }

    /* Check maximum packet size */
    if (remaining_len > MQTT_MAX_REMAINING_LENGTH) {
        return MQTT_ERR_PACKET_TOO_LARGE;
    }

    /* Fixed header */
    uint8_t fixed_header[6];
    size_t header_len = 0;
    fixed_header[header_len++] = MQTT_MAKE_FIXED_BYTE(MQTT_PACKET_SUBSCRIBE, MQTT_FLAGS_SUBSCRIBE);
    header_len += mqtt_varint_encode(remaining_len, fixed_header + header_len);

    mqtt_error_t err = mqtt_buffer_append(buf, fixed_header, header_len);
    if (err != MQTT_OK) {
        return err;
    }

    /* Variable header - Packet ID */
    uint8_t pid_buf[2];
    encode_uint16(pid_buf, packet_id);
    err = mqtt_buffer_append(buf, pid_buf, 2);
    if (err != MQTT_OK) {
        return err;
    }

    /* Payload - Topic filters with QoS */
    size_t total_len = header_len + 2;
    for (size_t i = 0; i < count; i++) {
        uint8_t topic_buf[2 + 65535];
        size_t filter_len = strlen(subs[i].topic_filter);
        int topic_len = mqtt_utf8_encode(subs[i].topic_filter, filter_len, topic_buf, sizeof(topic_buf));
        if (topic_len < 0) {
            return MQTT_ERR_INVALID_ARG;
        }
        err = mqtt_buffer_append(buf, topic_buf, topic_len);
        if (err != MQTT_OK) {
            return err;
        }

        uint8_t qos_byte = (uint8_t)subs[i].qos;
        err = mqtt_buffer_append(buf, &qos_byte, 1);
        if (err != MQTT_OK) {
            return err;
        }

        total_len += topic_len + 1;
    }

    return (ssize_t)total_len;
}

/* ========================================================================== */
/* UNSUBSCRIBE Packet Encoding                                               */
/* ========================================================================== */

ssize_t mqtt_v311_encode_unsubscribe(mqtt_buffer_t *buf, uint16_t packet_id,
                                     const char **topic_filters, size_t count)
{
    if (!buf || !topic_filters || count == 0 || packet_id == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Calculate remaining length */
    uint32_t remaining_len = 2;  /* Packet ID */

    for (size_t i = 0; i < count; i++) {
        if (!topic_filters[i]) {
            return MQTT_ERR_INVALID_ARG;
        }
        remaining_len += string_encoded_size(topic_filters[i]);
    }

    /* Check maximum packet size */
    if (remaining_len > MQTT_MAX_REMAINING_LENGTH) {
        return MQTT_ERR_PACKET_TOO_LARGE;
    }

    /* Fixed header */
    uint8_t fixed_header[6];
    size_t header_len = 0;
    fixed_header[header_len++] = MQTT_MAKE_FIXED_BYTE(MQTT_PACKET_UNSUBSCRIBE, MQTT_FLAGS_UNSUBSCRIBE);
    header_len += mqtt_varint_encode(remaining_len, fixed_header + header_len);

    mqtt_error_t err = mqtt_buffer_append(buf, fixed_header, header_len);
    if (err != MQTT_OK) {
        return err;
    }

    /* Variable header - Packet ID */
    uint8_t pid_buf[2];
    encode_uint16(pid_buf, packet_id);
    err = mqtt_buffer_append(buf, pid_buf, 2);
    if (err != MQTT_OK) {
        return err;
    }

    /* Payload - Topic filters */
    size_t total_len = header_len + 2;
    for (size_t i = 0; i < count; i++) {
        uint8_t topic_buf[2 + 65535];
        size_t filter_len = strlen(topic_filters[i]);
        int topic_len = mqtt_utf8_encode(topic_filters[i], filter_len, topic_buf, sizeof(topic_buf));
        if (topic_len < 0) {
            return MQTT_ERR_INVALID_ARG;
        }
        err = mqtt_buffer_append(buf, topic_buf, topic_len);
        if (err != MQTT_OK) {
            return err;
        }
        total_len += topic_len;
    }

    return (ssize_t)total_len;
}

/* ========================================================================== */
/* Control Packets (PINGREQ/PINGRESP/DISCONNECT)                            */
/* ========================================================================== */

/**
 * @brief Internal helper to encode zero-length packets
 *
 * PINGREQ, PINGRESP, and DISCONNECT have no variable header or payload.
 *
 * @param buf Output buffer
 * @param packet_type Packet type
 * @return Number of bytes written on success, negative error code on failure
 */
static ssize_t encode_zero_length_packet(mqtt_buffer_t *buf, mqtt_packet_type_t packet_type)
{
    if (!buf) {
        return MQTT_ERR_INVALID_ARG;
    }

    uint8_t packet[2];
    packet[0] = MQTT_MAKE_FIXED_BYTE(packet_type, 0);
    packet[1] = 0;  /* Remaining length = 0 */

    mqtt_error_t err = mqtt_buffer_append(buf, packet, 2);
    if (err != MQTT_OK) {
        return err;
    }

    return 2;
}

ssize_t mqtt_v311_encode_pingreq(mqtt_buffer_t *buf)
{
    return encode_zero_length_packet(buf, MQTT_PACKET_PINGREQ);
}

ssize_t mqtt_v311_encode_pingresp(mqtt_buffer_t *buf)
{
    return encode_zero_length_packet(buf, MQTT_PACKET_PINGRESP);
}

ssize_t mqtt_v311_encode_disconnect(mqtt_buffer_t *buf)
{
    return encode_zero_length_packet(buf, MQTT_PACKET_DISCONNECT);
}
