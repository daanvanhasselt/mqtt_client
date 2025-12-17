/**
 * @file mqtt_v5_codec.c
 * @brief MQTT 5.0 Protocol Codec Implementation
 *
 * Implements encoding and decoding for all MQTT 5.0 packet types.
 */

#define _POSIX_C_SOURCE 200809L

#include "mqtt_v5.h"
#include "../../core/mqtt_varint.h"
#include "../../core/mqtt_packet.h"
#include "../../memory/mqtt_memory.h"
#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/* Packet Type Constants                                                      */
/* ========================================================================== */

#define MQTT_PACKET_CONNECT     0x10
#define MQTT_PACKET_CONNACK     0x20
#define MQTT_PACKET_PUBLISH     0x30
#define MQTT_PACKET_PUBACK      0x40
#define MQTT_PACKET_PUBREC      0x50
#define MQTT_PACKET_PUBREL      0x62  /* Fixed flags: 0010 */
#define MQTT_PACKET_PUBCOMP     0x70
#define MQTT_PACKET_SUBSCRIBE   0x82  /* Fixed flags: 0010 */
#define MQTT_PACKET_SUBACK      0x90
#define MQTT_PACKET_UNSUBSCRIBE 0xA2  /* Fixed flags: 0010 */
#define MQTT_PACKET_UNSUBACK    0xB0
#define MQTT_PACKET_PINGREQ     0xC0
#define MQTT_PACKET_PINGRESP    0xD0
#define MQTT_PACKET_DISCONNECT  0xE0
#define MQTT_PACKET_AUTH        0xF0

/* Protocol name and version */
static const uint8_t MQTT_PROTOCOL_NAME[] = {0x00, 0x04, 'M', 'Q', 'T', 'T'};
#define MQTT_PROTOCOL_VERSION_5 5

/* ========================================================================== */
/* Helper Functions                                                           */
/* ========================================================================== */

/**
 * @brief Write fixed header and remaining length
 */
static ssize_t write_fixed_header(mqtt_buffer_t *buf, uint8_t packet_type, size_t remaining_len)
{
    /* Reserve space for fixed header (max 5 bytes: 1 type + 4 varint) */
    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 5) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }

    uint8_t *ptr = mqtt_buffer_write_ptr(buf);
    size_t start = mqtt_buffer_len(buf);

    /* Write packet type */
    *ptr++ = packet_type;

    /* Write remaining length as variable byte integer */
    int varint_len = mqtt_varint_encode((uint32_t)remaining_len, ptr);
    if (varint_len < 0) {
        return MQTT_ERR_PACKET_TOO_LARGE;
    }

    mqtt_buffer_advance_write(buf, 1 + (size_t)varint_len);
    return (ssize_t)(mqtt_buffer_len(buf) - start);
}

/**
 * @brief Write a UTF-8 string with length prefix
 */
static ssize_t write_utf8_string(mqtt_buffer_t *buf, const char *str)
{
    size_t len = str ? strlen(str) : 0;
    if (len > 65535) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 2 + len) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }

    uint8_t *ptr = mqtt_buffer_write_ptr(buf);
    *ptr++ = (len >> 8) & 0xFF;
    *ptr++ = len & 0xFF;
    if (len > 0) {
        memcpy(ptr, str, len);
    }

    mqtt_buffer_advance_write(buf, 2 + len);
    return (ssize_t)(2 + len);
}

/**
 * @brief Write binary data with length prefix
 */
static ssize_t write_binary_data(mqtt_buffer_t *buf, const uint8_t *data, size_t len)
{
    if (len > 65535) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 2 + len) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }

    uint8_t *ptr = mqtt_buffer_write_ptr(buf);
    *ptr++ = (len >> 8) & 0xFF;
    *ptr++ = len & 0xFF;
    if (len > 0 && data != NULL) {
        memcpy(ptr, data, len);
    }

    mqtt_buffer_advance_write(buf, 2 + len);
    return (ssize_t)(2 + len);
}

/* ========================================================================== */
/* CONNECT Encoding                                                           */
/* ========================================================================== */

ssize_t mqtt_v5_encode_connect(mqtt_buffer_t *buf, const mqtt_connect_opts_t *opts,
                                mqtt_property_t *properties)
{
    if (buf == NULL || opts == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    size_t start_pos = mqtt_buffer_len(buf);

    /* Calculate remaining length */
    size_t remaining_len = 0;

    /* Variable header */
    remaining_len += sizeof(MQTT_PROTOCOL_NAME);  /* Protocol name */
    remaining_len += 1;  /* Protocol version */
    remaining_len += 1;  /* Connect flags */
    remaining_len += 2;  /* Keep alive */
    remaining_len += mqtt_property_encoded_size(properties);  /* Properties */

    /* Payload */
    size_t client_id_len = opts->client_id ? strlen(opts->client_id) : 0;
    remaining_len += 2 + client_id_len;

    /* Will message */
    if (opts->will != NULL) {
        remaining_len += mqtt_property_encoded_size(NULL);  /* Will properties (empty for now) */
        remaining_len += 2 + (opts->will->topic ? strlen(opts->will->topic) : 0);
        remaining_len += 2 + opts->will->payload_len;
    }

    /* Username */
    if (opts->username != NULL) {
        remaining_len += 2 + strlen(opts->username);
    }

    /* Password */
    if (opts->password != NULL) {
        size_t pwd_len = opts->password_len > 0 ? opts->password_len : strlen((const char *)opts->password);
        remaining_len += 2 + pwd_len;
    }

    /* Write fixed header */
    ssize_t result = write_fixed_header(buf, MQTT_PACKET_CONNECT, remaining_len);
    if (result < 0) {
        return result;
    }

    /* Write protocol name */
    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + sizeof(MQTT_PROTOCOL_NAME)) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }
    memcpy(mqtt_buffer_write_ptr(buf), MQTT_PROTOCOL_NAME, sizeof(MQTT_PROTOCOL_NAME));
    mqtt_buffer_advance_write(buf, sizeof(MQTT_PROTOCOL_NAME));

    /* Write protocol version */
    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 1) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }
    *mqtt_buffer_write_ptr(buf) = MQTT_PROTOCOL_VERSION_5;
    mqtt_buffer_advance_write(buf, 1);

    /* Build connect flags */
    uint8_t flags = 0;
    if (opts->clean_start || opts->clean_session) {
        flags |= 0x02;  /* Clean Start */
    }
    if (opts->will != NULL) {
        flags |= 0x04;  /* Will Flag */
        flags |= ((opts->will->qos & 0x03) << 3);  /* Will QoS */
        if (opts->will->retain) {
            flags |= 0x20;  /* Will Retain */
        }
    }
    if (opts->password != NULL) {
        flags |= 0x40;  /* Password Flag */
    }
    if (opts->username != NULL) {
        flags |= 0x80;  /* Username Flag */
    }

    /* Write connect flags */
    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 1) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }
    *mqtt_buffer_write_ptr(buf) = flags;
    mqtt_buffer_advance_write(buf, 1);

    /* Write keep alive */
    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 2) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }
    uint8_t *ptr = mqtt_buffer_write_ptr(buf);
    *ptr++ = (opts->keepalive_sec >> 8) & 0xFF;
    *ptr++ = opts->keepalive_sec & 0xFF;
    mqtt_buffer_advance_write(buf, 2);

    /* Write properties */
    result = mqtt_property_encode(buf, properties);
    if (result < 0) {
        return result;
    }

    /* Write client ID */
    result = write_utf8_string(buf, opts->client_id ? opts->client_id : "");
    if (result < 0) {
        return result;
    }

    /* Write will message if present */
    if (opts->will != NULL) {
        /* Will properties (empty for now) */
        result = mqtt_property_encode(buf, NULL);
        if (result < 0) {
            return result;
        }

        /* Will topic */
        result = write_utf8_string(buf, opts->will->topic);
        if (result < 0) {
            return result;
        }

        /* Will payload */
        result = write_binary_data(buf, opts->will->payload, opts->will->payload_len);
        if (result < 0) {
            return result;
        }
    }

    /* Write username if present */
    if (opts->username != NULL) {
        result = write_utf8_string(buf, opts->username);
        if (result < 0) {
            return result;
        }
    }

    /* Write password if present */
    if (opts->password != NULL) {
        size_t pwd_len = opts->password_len > 0 ? opts->password_len : strlen((const char *)opts->password);
        result = write_binary_data(buf, opts->password, pwd_len);
        if (result < 0) {
            return result;
        }
    }

    return (ssize_t)(mqtt_buffer_len(buf) - start_pos);
}

/* ========================================================================== */
/* CONNACK Decoding                                                           */
/* ========================================================================== */

mqtt_error_t mqtt_v5_decode_connack(const uint8_t *buf, size_t len, mqtt_v5_connack_t *out)
{
    if (buf == NULL || out == NULL || len < 2) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(mqtt_v5_connack_t));

    /* Connect Acknowledge Flags */
    out->session_present = (buf[0] & 0x01) != 0;

    /* Reason Code */
    out->reason_code = (mqtt_reason_code_t)buf[1];

    /* Properties (optional) */
    if (len > 2) {
        size_t bytes_read;
        mqtt_error_t err = mqtt_property_decode(buf + 2, len - 2, &out->properties, &bytes_read);
        if (err != MQTT_OK) {
            return err;
        }
    }

    return MQTT_OK;
}

/* ========================================================================== */
/* PUBLISH Encoding/Decoding                                                  */
/* ========================================================================== */

ssize_t mqtt_v5_encode_publish(mqtt_buffer_t *buf, const mqtt_publish_opts_t *opts,
                                uint16_t packet_id, mqtt_property_t *properties)
{
    if (buf == NULL || opts == NULL || opts->topic == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    size_t start_pos = mqtt_buffer_len(buf);
    size_t topic_len = strlen(opts->topic);

    /* Calculate remaining length */
    size_t remaining_len = 2 + topic_len;  /* Topic */
    if (opts->qos > MQTT_QOS_0) {
        remaining_len += 2;  /* Packet ID */
    }
    remaining_len += mqtt_property_encoded_size(properties);
    remaining_len += opts->payload_len;

    /* Build packet type with flags */
    uint8_t packet_type = MQTT_PACKET_PUBLISH;
    if (opts->dup) packet_type |= 0x08;
    packet_type |= ((opts->qos & 0x03) << 1);
    if (opts->retain) packet_type |= 0x01;

    /* Write fixed header */
    ssize_t result = write_fixed_header(buf, packet_type, remaining_len);
    if (result < 0) {
        return result;
    }

    /* Write topic */
    result = write_utf8_string(buf, opts->topic);
    if (result < 0) {
        return result;
    }

    /* Write packet ID for QoS > 0 */
    if (opts->qos > MQTT_QOS_0) {
        if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 2) != MQTT_OK) {
            return MQTT_ERR_NOMEM;
        }
        uint8_t *ptr = mqtt_buffer_write_ptr(buf);
        *ptr++ = (packet_id >> 8) & 0xFF;
        *ptr++ = packet_id & 0xFF;
        mqtt_buffer_advance_write(buf, 2);
    }

    /* Write properties */
    result = mqtt_property_encode(buf, properties);
    if (result < 0) {
        return result;
    }

    /* Write payload */
    if (opts->payload_len > 0 && opts->payload != NULL) {
        if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + opts->payload_len) != MQTT_OK) {
            return MQTT_ERR_NOMEM;
        }
        memcpy(mqtt_buffer_write_ptr(buf), opts->payload, opts->payload_len);
        mqtt_buffer_advance_write(buf, opts->payload_len);
    }

    return (ssize_t)(mqtt_buffer_len(buf) - start_pos);
}

mqtt_error_t mqtt_v5_decode_publish(const uint8_t *buf, size_t len, uint8_t flags,
                                     mqtt_v5_publish_t *out)
{
    if (buf == NULL || out == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(mqtt_v5_publish_t));

    /* Extract flags */
    out->dup = (flags & 0x08) != 0;
    out->qos = (mqtt_qos_t)((flags >> 1) & 0x03);
    out->retain = (flags & 0x01) != 0;

    size_t offset = 0;

    /* Read topic */
    if (len < 2) {
        return MQTT_ERR_MALFORMED_PACKET;
    }
    out->topic_len = ((uint16_t)buf[0] << 8) | buf[1];
    offset += 2;

    if (len < offset + out->topic_len) {
        return MQTT_ERR_MALFORMED_PACKET;
    }
    out->topic = (const char *)(buf + offset);
    offset += out->topic_len;

    /* Read packet ID for QoS > 0 */
    if (out->qos > MQTT_QOS_0) {
        if (len < offset + 2) {
            return MQTT_ERR_MALFORMED_PACKET;
        }
        out->packet_id = ((uint16_t)buf[offset] << 8) | buf[offset + 1];
        offset += 2;
    }

    /* Read properties */
    size_t prop_bytes;
    mqtt_error_t err = mqtt_property_decode(buf + offset, len - offset, &out->properties, &prop_bytes);
    if (err != MQTT_OK) {
        return err;
    }
    offset += prop_bytes;

    /* Remaining is payload */
    out->payload = buf + offset;
    out->payload_len = len - offset;

    return MQTT_OK;
}

/* ========================================================================== */
/* ACK Packets (PUBACK/PUBREC/PUBREL/PUBCOMP)                                */
/* ========================================================================== */

static ssize_t encode_ack_packet(mqtt_buffer_t *buf, uint8_t packet_type, uint16_t packet_id,
                                  mqtt_reason_code_t reason_code, mqtt_property_t *properties)
{
    size_t start_pos = mqtt_buffer_len(buf);

    /* Calculate remaining length */
    size_t remaining_len = 2;  /* Packet ID */

    /* Reason code and properties only if needed */
    bool include_reason = (reason_code != MQTT_RC_SUCCESS) || (properties != NULL);
    if (include_reason) {
        remaining_len += 1;  /* Reason code */
        remaining_len += mqtt_property_encoded_size(properties);
    }

    /* Write fixed header */
    ssize_t result = write_fixed_header(buf, packet_type, remaining_len);
    if (result < 0) {
        return result;
    }

    /* Write packet ID */
    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 2) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }
    uint8_t *ptr = mqtt_buffer_write_ptr(buf);
    *ptr++ = (packet_id >> 8) & 0xFF;
    *ptr++ = packet_id & 0xFF;
    mqtt_buffer_advance_write(buf, 2);

    /* Write reason code and properties if needed */
    if (include_reason) {
        if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 1) != MQTT_OK) {
            return MQTT_ERR_NOMEM;
        }
        *mqtt_buffer_write_ptr(buf) = (uint8_t)reason_code;
        mqtt_buffer_advance_write(buf, 1);

        result = mqtt_property_encode(buf, properties);
        if (result < 0) {
            return result;
        }
    }

    return (ssize_t)(mqtt_buffer_len(buf) - start_pos);
}

ssize_t mqtt_v5_encode_puback(mqtt_buffer_t *buf, uint16_t packet_id,
                               mqtt_reason_code_t reason_code, mqtt_property_t *properties)
{
    return encode_ack_packet(buf, MQTT_PACKET_PUBACK, packet_id, reason_code, properties);
}

ssize_t mqtt_v5_encode_pubrec(mqtt_buffer_t *buf, uint16_t packet_id,
                               mqtt_reason_code_t reason_code, mqtt_property_t *properties)
{
    return encode_ack_packet(buf, MQTT_PACKET_PUBREC, packet_id, reason_code, properties);
}

ssize_t mqtt_v5_encode_pubrel(mqtt_buffer_t *buf, uint16_t packet_id,
                               mqtt_reason_code_t reason_code, mqtt_property_t *properties)
{
    return encode_ack_packet(buf, MQTT_PACKET_PUBREL, packet_id, reason_code, properties);
}

ssize_t mqtt_v5_encode_pubcomp(mqtt_buffer_t *buf, uint16_t packet_id,
                                mqtt_reason_code_t reason_code, mqtt_property_t *properties)
{
    return encode_ack_packet(buf, MQTT_PACKET_PUBCOMP, packet_id, reason_code, properties);
}

mqtt_error_t mqtt_v5_decode_ack(const uint8_t *buf, size_t len, mqtt_v5_ack_t *out)
{
    if (buf == NULL || out == NULL || len < 2) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(mqtt_v5_ack_t));

    /* Packet ID */
    out->packet_id = ((uint16_t)buf[0] << 8) | buf[1];

    /* Reason code (default success if not present) */
    out->reason_code = MQTT_RC_SUCCESS;
    if (len > 2) {
        out->reason_code = (mqtt_reason_code_t)buf[2];
    }

    /* Properties (optional) */
    if (len > 3) {
        size_t bytes_read;
        mqtt_error_t err = mqtt_property_decode(buf + 3, len - 3, &out->properties, &bytes_read);
        if (err != MQTT_OK) {
            return err;
        }
    }

    return MQTT_OK;
}

/* ========================================================================== */
/* SUBSCRIBE Encoding                                                         */
/* ========================================================================== */

ssize_t mqtt_v5_encode_subscribe(mqtt_buffer_t *buf, uint16_t packet_id,
                                  const mqtt_v5_subscription_t *subs, size_t count,
                                  mqtt_property_t *properties)
{
    if (buf == NULL || subs == NULL || count == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    size_t start_pos = mqtt_buffer_len(buf);

    /* Calculate remaining length */
    size_t remaining_len = 2;  /* Packet ID */
    remaining_len += mqtt_property_encoded_size(properties);

    for (size_t i = 0; i < count; i++) {
        if (subs[i].topic_filter == NULL) {
            return MQTT_ERR_INVALID_ARG;
        }
        remaining_len += 2 + strlen(subs[i].topic_filter);  /* Topic filter */
        remaining_len += 1;  /* Subscription options */
    }

    /* Write fixed header */
    ssize_t result = write_fixed_header(buf, MQTT_PACKET_SUBSCRIBE, remaining_len);
    if (result < 0) {
        return result;
    }

    /* Write packet ID */
    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 2) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }
    uint8_t *ptr = mqtt_buffer_write_ptr(buf);
    *ptr++ = (packet_id >> 8) & 0xFF;
    *ptr++ = packet_id & 0xFF;
    mqtt_buffer_advance_write(buf, 2);

    /* Write properties */
    result = mqtt_property_encode(buf, properties);
    if (result < 0) {
        return result;
    }

    /* Write subscriptions */
    for (size_t i = 0; i < count; i++) {
        /* Topic filter */
        result = write_utf8_string(buf, subs[i].topic_filter);
        if (result < 0) {
            return result;
        }

        /* Subscription options byte */
        uint8_t options = subs[i].max_qos & 0x03;
        if (subs[i].no_local) options |= 0x04;
        if (subs[i].retain_as_published) options |= 0x08;
        options |= ((subs[i].retain_handling & 0x03) << 4);

        if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 1) != MQTT_OK) {
            return MQTT_ERR_NOMEM;
        }
        *mqtt_buffer_write_ptr(buf) = options;
        mqtt_buffer_advance_write(buf, 1);
    }

    return (ssize_t)(mqtt_buffer_len(buf) - start_pos);
}

/* ========================================================================== */
/* SUBACK Decoding                                                            */
/* ========================================================================== */

mqtt_error_t mqtt_v5_decode_suback(const uint8_t *buf, size_t len, mqtt_v5_suback_t *out)
{
    if (buf == NULL || out == NULL || len < 3) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(mqtt_v5_suback_t));

    size_t offset = 0;

    /* Packet ID */
    out->packet_id = ((uint16_t)buf[0] << 8) | buf[1];
    offset += 2;

    /* Properties */
    size_t prop_bytes;
    mqtt_error_t err = mqtt_property_decode(buf + offset, len - offset, &out->properties, &prop_bytes);
    if (err != MQTT_OK) {
        return err;
    }
    offset += prop_bytes;

    /* Reason codes */
    out->count = len - offset;
    if (out->count > 0) {
        out->reason_codes = malloc(out->count * sizeof(mqtt_reason_code_t));
        if (out->reason_codes == NULL) {
            mqtt_property_list_free(out->properties);
            return MQTT_ERR_NOMEM;
        }
        for (size_t i = 0; i < out->count; i++) {
            out->reason_codes[i] = (mqtt_reason_code_t)buf[offset + i];
        }
    }

    return MQTT_OK;
}

/* ========================================================================== */
/* UNSUBSCRIBE Encoding                                                       */
/* ========================================================================== */

ssize_t mqtt_v5_encode_unsubscribe(mqtt_buffer_t *buf, uint16_t packet_id,
                                    const char **topic_filters, size_t count,
                                    mqtt_property_t *properties)
{
    if (buf == NULL || topic_filters == NULL || count == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    size_t start_pos = mqtt_buffer_len(buf);

    /* Calculate remaining length */
    size_t remaining_len = 2;  /* Packet ID */
    remaining_len += mqtt_property_encoded_size(properties);

    for (size_t i = 0; i < count; i++) {
        if (topic_filters[i] == NULL) {
            return MQTT_ERR_INVALID_ARG;
        }
        remaining_len += 2 + strlen(topic_filters[i]);
    }

    /* Write fixed header */
    ssize_t result = write_fixed_header(buf, MQTT_PACKET_UNSUBSCRIBE, remaining_len);
    if (result < 0) {
        return result;
    }

    /* Write packet ID */
    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 2) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }
    uint8_t *ptr = mqtt_buffer_write_ptr(buf);
    *ptr++ = (packet_id >> 8) & 0xFF;
    *ptr++ = packet_id & 0xFF;
    mqtt_buffer_advance_write(buf, 2);

    /* Write properties */
    result = mqtt_property_encode(buf, properties);
    if (result < 0) {
        return result;
    }

    /* Write topic filters */
    for (size_t i = 0; i < count; i++) {
        result = write_utf8_string(buf, topic_filters[i]);
        if (result < 0) {
            return result;
        }
    }

    return (ssize_t)(mqtt_buffer_len(buf) - start_pos);
}

/* ========================================================================== */
/* UNSUBACK Decoding                                                          */
/* ========================================================================== */

mqtt_error_t mqtt_v5_decode_unsuback(const uint8_t *buf, size_t len, mqtt_v5_unsuback_t *out)
{
    if (buf == NULL || out == NULL || len < 3) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(mqtt_v5_unsuback_t));

    size_t offset = 0;

    /* Packet ID */
    out->packet_id = ((uint16_t)buf[0] << 8) | buf[1];
    offset += 2;

    /* Properties */
    size_t prop_bytes;
    mqtt_error_t err = mqtt_property_decode(buf + offset, len - offset, &out->properties, &prop_bytes);
    if (err != MQTT_OK) {
        return err;
    }
    offset += prop_bytes;

    /* Reason codes */
    out->count = len - offset;
    if (out->count > 0) {
        out->reason_codes = malloc(out->count * sizeof(mqtt_reason_code_t));
        if (out->reason_codes == NULL) {
            mqtt_property_list_free(out->properties);
            return MQTT_ERR_NOMEM;
        }
        for (size_t i = 0; i < out->count; i++) {
            out->reason_codes[i] = (mqtt_reason_code_t)buf[offset + i];
        }
    }

    return MQTT_OK;
}

/* ========================================================================== */
/* DISCONNECT Encoding/Decoding                                               */
/* ========================================================================== */

ssize_t mqtt_v5_encode_disconnect(mqtt_buffer_t *buf, mqtt_reason_code_t reason_code,
                                   mqtt_property_t *properties)
{
    if (buf == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    size_t start_pos = mqtt_buffer_len(buf);

    /* Calculate remaining length */
    bool include_reason = (reason_code != MQTT_RC_NORMAL_DISCONNECTION) || (properties != NULL);
    size_t remaining_len = 0;
    if (include_reason) {
        remaining_len = 1;  /* Reason code */
        remaining_len += mqtt_property_encoded_size(properties);
    }

    /* Write fixed header */
    ssize_t result = write_fixed_header(buf, MQTT_PACKET_DISCONNECT, remaining_len);
    if (result < 0) {
        return result;
    }

    /* Write reason code and properties if needed */
    if (include_reason) {
        if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 1) != MQTT_OK) {
            return MQTT_ERR_NOMEM;
        }
        *mqtt_buffer_write_ptr(buf) = (uint8_t)reason_code;
        mqtt_buffer_advance_write(buf, 1);

        result = mqtt_property_encode(buf, properties);
        if (result < 0) {
            return result;
        }
    }

    return (ssize_t)(mqtt_buffer_len(buf) - start_pos);
}

mqtt_error_t mqtt_v5_decode_disconnect(const uint8_t *buf, size_t len, mqtt_v5_disconnect_t *out)
{
    if (out == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(mqtt_v5_disconnect_t));

    /* Default reason code */
    out->reason_code = MQTT_RC_NORMAL_DISCONNECTION;

    if (buf == NULL || len == 0) {
        return MQTT_OK;  /* Empty DISCONNECT is valid */
    }

    /* Reason code */
    out->reason_code = (mqtt_reason_code_t)buf[0];

    /* Properties (optional) */
    if (len > 1) {
        size_t bytes_read;
        mqtt_error_t err = mqtt_property_decode(buf + 1, len - 1, &out->properties, &bytes_read);
        if (err != MQTT_OK) {
            return err;
        }
    }

    return MQTT_OK;
}

/* ========================================================================== */
/* AUTH Encoding/Decoding                                                     */
/* ========================================================================== */

ssize_t mqtt_v5_encode_auth(mqtt_buffer_t *buf, mqtt_reason_code_t reason_code,
                             mqtt_property_t *properties)
{
    if (buf == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    size_t start_pos = mqtt_buffer_len(buf);

    /* Calculate remaining length */
    size_t remaining_len = 1;  /* Reason code */
    remaining_len += mqtt_property_encoded_size(properties);

    /* Write fixed header */
    ssize_t result = write_fixed_header(buf, MQTT_PACKET_AUTH, remaining_len);
    if (result < 0) {
        return result;
    }

    /* Write reason code */
    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + 1) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }
    *mqtt_buffer_write_ptr(buf) = (uint8_t)reason_code;
    mqtt_buffer_advance_write(buf, 1);

    /* Write properties */
    result = mqtt_property_encode(buf, properties);
    if (result < 0) {
        return result;
    }

    return (ssize_t)(mqtt_buffer_len(buf) - start_pos);
}

mqtt_error_t mqtt_v5_decode_auth(const uint8_t *buf, size_t len, mqtt_v5_auth_t *out)
{
    if (buf == NULL || out == NULL || len < 1) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(mqtt_v5_auth_t));

    /* Reason code */
    out->reason_code = (mqtt_reason_code_t)buf[0];

    /* Properties */
    if (len > 1) {
        size_t bytes_read;
        mqtt_error_t err = mqtt_property_decode(buf + 1, len - 1, &out->properties, &bytes_read);
        if (err != MQTT_OK) {
            return err;
        }
    }

    return MQTT_OK;
}

/* ========================================================================== */
/* Control Packets (PINGREQ/PINGRESP)                                        */
/* ========================================================================== */

ssize_t mqtt_v5_encode_pingreq(mqtt_buffer_t *buf)
{
    if (buf == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    return write_fixed_header(buf, MQTT_PACKET_PINGREQ, 0);
}

ssize_t mqtt_v5_encode_pingresp(mqtt_buffer_t *buf)
{
    if (buf == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    return write_fixed_header(buf, MQTT_PACKET_PINGRESP, 0);
}

/* ========================================================================== */
/* Utility Functions                                                          */
/* ========================================================================== */

const char *mqtt_v5_reason_code_str(mqtt_reason_code_t code)
{
    switch (code) {
        case MQTT_RC_SUCCESS:                   return "Success";
        case MQTT_RC_GRANTED_QOS_1:             return "Granted QoS 1";
        case MQTT_RC_GRANTED_QOS_2:             return "Granted QoS 2";
        case MQTT_RC_DISCONNECT_WITH_WILL:      return "Disconnect with Will";
        case MQTT_RC_NO_MATCHING_SUBSCRIBERS:   return "No matching subscribers";
        case MQTT_RC_NO_SUBSCRIPTION_EXISTED:   return "No subscription existed";
        case MQTT_RC_CONTINUE_AUTHENTICATION:   return "Continue authentication";
        case MQTT_RC_REAUTHENTICATE:            return "Re-authenticate";
        case MQTT_RC_UNSPECIFIED_ERROR:         return "Unspecified error";
        case MQTT_RC_MALFORMED_PACKET:          return "Malformed packet";
        case MQTT_RC_PROTOCOL_ERROR:            return "Protocol error";
        case MQTT_RC_IMPLEMENTATION_SPECIFIC:   return "Implementation specific error";
        case MQTT_RC_UNSUPPORTED_PROTOCOL_VERSION: return "Unsupported protocol version";
        case MQTT_RC_CLIENT_ID_NOT_VALID:       return "Client ID not valid";
        case MQTT_RC_BAD_USERNAME_OR_PASSWORD:  return "Bad username or password";
        case MQTT_RC_NOT_AUTHORIZED:            return "Not authorized";
        case MQTT_RC_SERVER_UNAVAILABLE:        return "Server unavailable";
        case MQTT_RC_SERVER_BUSY:               return "Server busy";
        case MQTT_RC_BANNED:                    return "Banned";
        case MQTT_RC_SERVER_SHUTTING_DOWN:      return "Server shutting down";
        case MQTT_RC_BAD_AUTHENTICATION_METHOD: return "Bad authentication method";
        case MQTT_RC_KEEP_ALIVE_TIMEOUT:        return "Keep alive timeout";
        case MQTT_RC_SESSION_TAKEN_OVER:        return "Session taken over";
        case MQTT_RC_TOPIC_FILTER_INVALID:      return "Topic filter invalid";
        case MQTT_RC_TOPIC_NAME_INVALID:        return "Topic name invalid";
        case MQTT_RC_PACKET_ID_IN_USE:          return "Packet ID in use";
        case MQTT_RC_PACKET_ID_NOT_FOUND:       return "Packet ID not found";
        case MQTT_RC_RECEIVE_MAXIMUM_EXCEEDED:  return "Receive maximum exceeded";
        case MQTT_RC_TOPIC_ALIAS_INVALID:       return "Topic alias invalid";
        case MQTT_RC_PACKET_TOO_LARGE:          return "Packet too large";
        case MQTT_RC_MESSAGE_RATE_TOO_HIGH:     return "Message rate too high";
        case MQTT_RC_QUOTA_EXCEEDED:            return "Quota exceeded";
        case MQTT_RC_ADMINISTRATIVE_ACTION:     return "Administrative action";
        case MQTT_RC_PAYLOAD_FORMAT_INVALID:    return "Payload format invalid";
        case MQTT_RC_RETAIN_NOT_SUPPORTED:      return "Retain not supported";
        case MQTT_RC_QOS_NOT_SUPPORTED:         return "QoS not supported";
        case MQTT_RC_USE_ANOTHER_SERVER:        return "Use another server";
        case MQTT_RC_SERVER_MOVED:              return "Server moved";
        case MQTT_RC_SHARED_SUBS_NOT_SUPPORTED: return "Shared subscriptions not supported";
        case MQTT_RC_CONNECTION_RATE_EXCEEDED:  return "Connection rate exceeded";
        case MQTT_RC_MAXIMUM_CONNECT_TIME:      return "Maximum connect time";
        case MQTT_RC_SUBSCRIPTION_IDS_NOT_SUPPORTED: return "Subscription IDs not supported";
        case MQTT_RC_WILDCARD_SUBS_NOT_SUPPORTED: return "Wildcard subscriptions not supported";
        default:                                return "Unknown";
    }
}

bool mqtt_v5_reason_code_success(mqtt_reason_code_t code)
{
    return code < 0x80;
}
