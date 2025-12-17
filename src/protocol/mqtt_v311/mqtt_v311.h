/**
 * @file mqtt_v311.h
 * @brief MQTT 3.1.1 Protocol Codec Interface
 *
 * This header defines the encoding and decoding functions for MQTT 3.1.1 packets.
 * It provides a complete implementation of the MQTT 3.1.1 protocol specification
 * for packet serialization and deserialization.
 *
 * @note This is an internal header - not part of the public API
 */

#ifndef MQTT_V311_H
#define MQTT_V311_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <mqtt/mqtt_types.h>
#include <mqtt/mqtt_error.h>
#include "../../memory/mqtt_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* CONNECT Packet Encoding                                                    */
/* ========================================================================== */

/**
 * @brief Encode MQTT 3.1.1 CONNECT packet
 *
 * Encodes a CONNECT packet according to MQTT 3.1.1 specification.
 * The packet contains:
 * - Protocol name ("MQTT")
 * - Protocol level (4)
 * - Connect flags (clean session, will, username, password)
 * - Keep-alive timer
 * - Client identifier
 * - Will topic and message (if will flag set)
 * - Username and password (if flags set)
 *
 * @param buf Output buffer to write encoded packet
 * @param opts Connection options containing all required parameters
 * @return Number of bytes written on success, negative error code on failure
 *
 * @note The buffer will be automatically grown if needed
 * @note Returns MQTT_ERR_INVALID_ARG if opts is NULL or contains invalid data
 */
ssize_t mqtt_v311_encode_connect(mqtt_buffer_t *buf, const mqtt_connect_opts_t *opts);

/* ========================================================================== */
/* CONNACK Packet Decoding                                                    */
/* ========================================================================== */

/**
 * @brief MQTT 3.1.1 CONNACK packet structure
 */
typedef struct mqtt_v311_connack {
    bool session_present;    /**< Session present flag (SP) */
    uint8_t return_code;     /**< Connect return code: 0=accepted, 1-5=various errors */
} mqtt_v311_connack_t;

/**
 * @brief Decode MQTT 3.1.1 CONNACK packet
 *
 * Parses a CONNACK packet and extracts the session present flag and return code.
 *
 * Return codes:
 * - 0x00: Connection Accepted
 * - 0x01: Unacceptable protocol version
 * - 0x02: Identifier rejected
 * - 0x03: Server unavailable
 * - 0x04: Bad username or password
 * - 0x05: Not authorized
 *
 * @param buf Input buffer containing packet data (without fixed header)
 * @param len Length of data in buffer
 * @param out Output structure to receive parsed data
 * @return MQTT_OK on success, error code on failure
 *
 * @note buf should point to the variable header (after remaining length)
 */
mqtt_error_t mqtt_v311_decode_connack(const uint8_t *buf, size_t len, mqtt_v311_connack_t *out);

/* ========================================================================== */
/* PUBLISH Packet Encoding/Decoding                                          */
/* ========================================================================== */

/**
 * @brief Encode MQTT 3.1.1 PUBLISH packet
 *
 * Encodes a PUBLISH packet with:
 * - Topic name
 * - Packet ID (for QoS > 0)
 * - Payload
 *
 * @param buf Output buffer to write encoded packet
 * @param opts Publish options (topic, payload, QoS, retain, dup)
 * @param packet_id Packet identifier (only used if QoS > 0, ignored for QoS 0)
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v311_encode_publish(mqtt_buffer_t *buf, const mqtt_publish_opts_t *opts, uint16_t packet_id);

/**
 * @brief MQTT 3.1.1 PUBLISH packet structure
 */
typedef struct mqtt_v311_publish {
    const char *topic;       /**< Topic name (not null-terminated) */
    uint16_t topic_len;      /**< Length of topic name */
    const uint8_t *payload;  /**< Message payload */
    size_t payload_len;      /**< Length of payload */
    mqtt_qos_t qos;          /**< Quality of Service level */
    bool retain;             /**< Retain flag */
    bool dup;                /**< Duplicate delivery flag */
    uint16_t packet_id;      /**< Packet identifier (only for QoS > 0) */
} mqtt_v311_publish_t;

/**
 * @brief Decode MQTT 3.1.1 PUBLISH packet
 *
 * Parses a PUBLISH packet and extracts all fields.
 *
 * @param buf Input buffer containing packet data (without fixed header)
 * @param len Length of data in buffer
 * @param flags Flags from fixed header (contains DUP, QoS, RETAIN)
 * @param out Output structure to receive parsed data
 * @return MQTT_OK on success, error code on failure
 *
 * @note The topic and payload pointers will point into the input buffer
 * @note Caller must ensure the buffer remains valid while using the parsed data
 */
mqtt_error_t mqtt_v311_decode_publish(const uint8_t *buf, size_t len, uint8_t flags, mqtt_v311_publish_t *out);

/* ========================================================================== */
/* PUBACK/PUBREC/PUBREL/PUBCOMP Encoding/Decoding                            */
/* ========================================================================== */

/**
 * @brief Encode MQTT 3.1.1 PUBACK packet
 *
 * Encodes a PUBACK packet (QoS 1 acknowledgment).
 *
 * @param buf Output buffer to write encoded packet
 * @param packet_id Packet identifier from the PUBLISH being acknowledged
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v311_encode_puback(mqtt_buffer_t *buf, uint16_t packet_id);

/**
 * @brief Encode MQTT 3.1.1 PUBREC packet
 *
 * Encodes a PUBREC packet (QoS 2 delivery, part 1).
 *
 * @param buf Output buffer to write encoded packet
 * @param packet_id Packet identifier from the PUBLISH
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v311_encode_pubrec(mqtt_buffer_t *buf, uint16_t packet_id);

/**
 * @brief Encode MQTT 3.1.1 PUBREL packet
 *
 * Encodes a PUBREL packet (QoS 2 delivery, part 2).
 *
 * @param buf Output buffer to write encoded packet
 * @param packet_id Packet identifier from the PUBLISH
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v311_encode_pubrel(mqtt_buffer_t *buf, uint16_t packet_id);

/**
 * @brief Encode MQTT 3.1.1 PUBCOMP packet
 *
 * Encodes a PUBCOMP packet (QoS 2 delivery, part 3).
 *
 * @param buf Output buffer to write encoded packet
 * @param packet_id Packet identifier from the PUBLISH
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v311_encode_pubcomp(mqtt_buffer_t *buf, uint16_t packet_id);

/**
 * @brief Decode MQTT 3.1.1 acknowledgment packet (PUBACK/PUBREC/PUBREL/PUBCOMP)
 *
 * All QoS acknowledgment packets have the same format: just a 2-byte packet ID.
 * This function can decode any of them.
 *
 * @param buf Input buffer containing packet data (without fixed header)
 * @param len Length of data in buffer
 * @param packet_id Output pointer to receive packet identifier
 * @return MQTT_OK on success, error code on failure
 */
mqtt_error_t mqtt_v311_decode_ack(const uint8_t *buf, size_t len, uint16_t *packet_id);

/* ========================================================================== */
/* SUBSCRIBE Packet Encoding                                                 */
/* ========================================================================== */

/**
 * @brief MQTT 3.1.1 subscription entry
 */
typedef struct mqtt_v311_subscription {
    const char *topic_filter; /**< Topic filter string */
    mqtt_qos_t qos;           /**< Requested QoS level (0, 1, or 2) */
} mqtt_v311_subscription_t;

/**
 * @brief Encode MQTT 3.1.1 SUBSCRIBE packet
 *
 * Encodes a SUBSCRIBE packet with one or more topic filter subscriptions.
 *
 * @param buf Output buffer to write encoded packet
 * @param packet_id Packet identifier for this subscription request
 * @param subs Array of subscription entries
 * @param count Number of subscriptions in the array
 * @return Number of bytes written on success, negative error code on failure
 *
 * @note At least one subscription must be provided (count > 0)
 */
ssize_t mqtt_v311_encode_subscribe(mqtt_buffer_t *buf, uint16_t packet_id,
                                   const mqtt_v311_subscription_t *subs, size_t count);

/* ========================================================================== */
/* SUBACK Decoding                                                           */
/* ========================================================================== */

/**
 * @brief MQTT 3.1.1 SUBACK packet structure
 */
typedef struct mqtt_v311_suback {
    uint16_t packet_id;       /**< Packet identifier from SUBSCRIBE */
    uint8_t *return_codes;    /**< Array of return codes (0,1,2 = QoS granted, 0x80 = failure) */
    size_t count;             /**< Number of return codes */
} mqtt_v311_suback_t;

/**
 * @brief Decode MQTT 3.1.1 SUBACK packet
 *
 * Parses a SUBACK packet and extracts the granted QoS levels for each subscription.
 *
 * Return codes:
 * - 0x00: Maximum QoS 0 granted
 * - 0x01: Maximum QoS 1 granted
 * - 0x02: Maximum QoS 2 granted
 * - 0x80: Subscription failure
 *
 * @param buf Input buffer containing packet data (without fixed header)
 * @param len Length of data in buffer
 * @param out Output structure to receive parsed data
 * @return MQTT_OK on success, error code on failure
 *
 * @note The return_codes array must be freed by the caller
 */
mqtt_error_t mqtt_v311_decode_suback(const uint8_t *buf, size_t len, mqtt_v311_suback_t *out);

/* ========================================================================== */
/* UNSUBSCRIBE Packet Encoding                                               */
/* ========================================================================== */

/**
 * @brief Encode MQTT 3.1.1 UNSUBSCRIBE packet
 *
 * Encodes an UNSUBSCRIBE packet with one or more topic filters.
 *
 * @param buf Output buffer to write encoded packet
 * @param packet_id Packet identifier for this unsubscribe request
 * @param topic_filters Array of topic filter strings
 * @param count Number of topic filters in the array
 * @return Number of bytes written on success, negative error code on failure
 *
 * @note At least one topic filter must be provided (count > 0)
 */
ssize_t mqtt_v311_encode_unsubscribe(mqtt_buffer_t *buf, uint16_t packet_id,
                                     const char **topic_filters, size_t count);

/* ========================================================================== */
/* UNSUBACK Decoding                                                         */
/* ========================================================================== */

/**
 * @brief Decode MQTT 3.1.1 UNSUBACK packet
 *
 * Parses an UNSUBACK packet and extracts the packet identifier.
 *
 * @param buf Input buffer containing packet data (without fixed header)
 * @param len Length of data in buffer
 * @param packet_id Output pointer to receive packet identifier
 * @return MQTT_OK on success, error code on failure
 */
mqtt_error_t mqtt_v311_decode_unsuback(const uint8_t *buf, size_t len, uint16_t *packet_id);

/* ========================================================================== */
/* Control Packets (PINGREQ/PINGRESP/DISCONNECT)                            */
/* ========================================================================== */

/**
 * @brief Encode MQTT 3.1.1 PINGREQ packet
 *
 * Encodes a PINGREQ packet (keep-alive ping).
 *
 * @param buf Output buffer to write encoded packet
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v311_encode_pingreq(mqtt_buffer_t *buf);

/**
 * @brief Encode MQTT 3.1.1 PINGRESP packet
 *
 * Encodes a PINGRESP packet (keep-alive ping response).
 *
 * @param buf Output buffer to write encoded packet
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v311_encode_pingresp(mqtt_buffer_t *buf);

/**
 * @brief Encode MQTT 3.1.1 DISCONNECT packet
 *
 * Encodes a DISCONNECT packet (graceful disconnect).
 *
 * @param buf Output buffer to write encoded packet
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v311_encode_disconnect(mqtt_buffer_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_V311_H */
