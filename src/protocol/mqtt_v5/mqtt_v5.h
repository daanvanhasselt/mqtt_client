/**
 * @file mqtt_v5.h
 * @brief MQTT 5.0 Protocol Codec Interface
 *
 * This header defines the encoding and decoding functions for MQTT 5.0 packets.
 * It provides a complete implementation of the MQTT 5.0 protocol specification
 * for packet serialization and deserialization, including properties support.
 *
 * @note This is an internal header - not part of the public API
 */

#ifndef MQTT_V5_H
#define MQTT_V5_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <mqtt/mqtt_types.h>
#include <mqtt/mqtt_error.h>
#include "../../memory/mqtt_buffer.h"
#include "mqtt_v5_properties.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* MQTT 5.0 Reason Codes                                                      */
/* ========================================================================== */

/**
 * @brief MQTT 5.0 Reason Codes
 *
 * These reason codes are used in CONNACK, PUBACK, PUBREC, PUBREL, PUBCOMP,
 * SUBACK, UNSUBACK, DISCONNECT, and AUTH packets.
 */
typedef enum mqtt_reason_code {
    /* Success codes */
    MQTT_RC_SUCCESS                     = 0x00,  /* All packets */
    MQTT_RC_NORMAL_DISCONNECTION        = 0x00,  /* DISCONNECT */
    MQTT_RC_GRANTED_QOS_0               = 0x00,  /* SUBACK */
    MQTT_RC_GRANTED_QOS_1               = 0x01,  /* SUBACK */
    MQTT_RC_GRANTED_QOS_2               = 0x02,  /* SUBACK */
    MQTT_RC_DISCONNECT_WITH_WILL        = 0x04,  /* DISCONNECT */
    MQTT_RC_NO_MATCHING_SUBSCRIBERS     = 0x10,  /* PUBACK, PUBREC */
    MQTT_RC_NO_SUBSCRIPTION_EXISTED     = 0x11,  /* UNSUBACK */
    MQTT_RC_CONTINUE_AUTHENTICATION     = 0x18,  /* AUTH */
    MQTT_RC_REAUTHENTICATE              = 0x19,  /* AUTH */

    /* Error codes (0x80+) */
    MQTT_RC_UNSPECIFIED_ERROR           = 0x80,
    MQTT_RC_MALFORMED_PACKET            = 0x81,
    MQTT_RC_PROTOCOL_ERROR              = 0x82,
    MQTT_RC_IMPLEMENTATION_SPECIFIC     = 0x83,
    MQTT_RC_UNSUPPORTED_PROTOCOL_VERSION = 0x84,  /* CONNACK */
    MQTT_RC_CLIENT_ID_NOT_VALID         = 0x85,  /* CONNACK */
    MQTT_RC_BAD_USERNAME_OR_PASSWORD    = 0x86,  /* CONNACK */
    MQTT_RC_NOT_AUTHORIZED              = 0x87,
    MQTT_RC_SERVER_UNAVAILABLE          = 0x88,  /* CONNACK */
    MQTT_RC_SERVER_BUSY                 = 0x89,
    MQTT_RC_BANNED                      = 0x8A,  /* CONNACK */
    MQTT_RC_SERVER_SHUTTING_DOWN        = 0x8B,  /* DISCONNECT */
    MQTT_RC_BAD_AUTHENTICATION_METHOD   = 0x8C,  /* CONNACK, DISCONNECT */
    MQTT_RC_KEEP_ALIVE_TIMEOUT          = 0x8D,  /* DISCONNECT */
    MQTT_RC_SESSION_TAKEN_OVER          = 0x8E,  /* DISCONNECT */
    MQTT_RC_TOPIC_FILTER_INVALID        = 0x8F,
    MQTT_RC_TOPIC_NAME_INVALID          = 0x90,
    MQTT_RC_PACKET_ID_IN_USE            = 0x91,  /* PUBACK, PUBREC, SUBACK, UNSUBACK */
    MQTT_RC_PACKET_ID_NOT_FOUND         = 0x92,  /* PUBREL, PUBCOMP */
    MQTT_RC_RECEIVE_MAXIMUM_EXCEEDED    = 0x93,  /* DISCONNECT */
    MQTT_RC_TOPIC_ALIAS_INVALID         = 0x94,  /* DISCONNECT */
    MQTT_RC_PACKET_TOO_LARGE            = 0x95,
    MQTT_RC_MESSAGE_RATE_TOO_HIGH       = 0x96,  /* DISCONNECT */
    MQTT_RC_QUOTA_EXCEEDED              = 0x97,
    MQTT_RC_ADMINISTRATIVE_ACTION       = 0x98,  /* DISCONNECT */
    MQTT_RC_PAYLOAD_FORMAT_INVALID      = 0x99,
    MQTT_RC_RETAIN_NOT_SUPPORTED        = 0x9A,  /* CONNACK, DISCONNECT */
    MQTT_RC_QOS_NOT_SUPPORTED           = 0x9B,  /* CONNACK, DISCONNECT */
    MQTT_RC_USE_ANOTHER_SERVER          = 0x9C,  /* CONNACK, DISCONNECT */
    MQTT_RC_SERVER_MOVED                = 0x9D,  /* CONNACK, DISCONNECT */
    MQTT_RC_SHARED_SUBS_NOT_SUPPORTED   = 0x9E,  /* SUBACK, DISCONNECT */
    MQTT_RC_CONNECTION_RATE_EXCEEDED    = 0x9F,  /* CONNACK, DISCONNECT */
    MQTT_RC_MAXIMUM_CONNECT_TIME        = 0xA0,  /* DISCONNECT */
    MQTT_RC_SUBSCRIPTION_IDS_NOT_SUPPORTED = 0xA1,  /* SUBACK, DISCONNECT */
    MQTT_RC_WILDCARD_SUBS_NOT_SUPPORTED = 0xA2   /* SUBACK, DISCONNECT */
} mqtt_reason_code_t;

/* ========================================================================== */
/* CONNECT Packet Encoding                                                    */
/* ========================================================================== */

/**
 * @brief Encode MQTT 5.0 CONNECT packet
 *
 * Encodes a CONNECT packet according to MQTT 5.0 specification.
 * Includes properties support for session expiry, receive maximum, etc.
 *
 * @param buf Output buffer to write encoded packet
 * @param opts Connection options containing all required parameters
 * @param properties Optional properties list (can be NULL)
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v5_encode_connect(mqtt_buffer_t *buf, const mqtt_connect_opts_t *opts,
                                mqtt_property_t *properties);

/* ========================================================================== */
/* CONNACK Packet Decoding                                                    */
/* ========================================================================== */

/**
 * @brief MQTT 5.0 CONNACK packet structure
 */
typedef struct mqtt_v5_connack {
    bool session_present;           /**< Session present flag */
    mqtt_reason_code_t reason_code; /**< Connect reason code */
    mqtt_property_t *properties;    /**< Properties list (caller must free) */
} mqtt_v5_connack_t;

/**
 * @brief Decode MQTT 5.0 CONNACK packet
 *
 * @param buf Input buffer containing packet data (without fixed header)
 * @param len Length of data in buffer
 * @param out Output structure to receive parsed data
 * @return MQTT_OK on success, error code on failure
 *
 * @note Caller must free out->properties using mqtt_property_list_free()
 */
mqtt_error_t mqtt_v5_decode_connack(const uint8_t *buf, size_t len, mqtt_v5_connack_t *out);

/* ========================================================================== */
/* PUBLISH Packet Encoding/Decoding                                           */
/* ========================================================================== */

/**
 * @brief Encode MQTT 5.0 PUBLISH packet
 *
 * @param buf Output buffer to write encoded packet
 * @param opts Publish options (topic, payload, QoS, retain, dup)
 * @param packet_id Packet identifier (only used if QoS > 0)
 * @param properties Optional properties list (can be NULL)
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v5_encode_publish(mqtt_buffer_t *buf, const mqtt_publish_opts_t *opts,
                                uint16_t packet_id, mqtt_property_t *properties);

/**
 * @brief MQTT 5.0 PUBLISH packet structure
 */
typedef struct mqtt_v5_publish {
    const char *topic;              /**< Topic name (not null-terminated) */
    uint16_t topic_len;             /**< Length of topic name */
    const uint8_t *payload;         /**< Message payload */
    size_t payload_len;             /**< Length of payload */
    mqtt_qos_t qos;                 /**< Quality of Service level */
    bool retain;                    /**< Retain flag */
    bool dup;                       /**< Duplicate delivery flag */
    uint16_t packet_id;             /**< Packet identifier (only for QoS > 0) */
    mqtt_property_t *properties;    /**< Properties list (caller must free) */
} mqtt_v5_publish_t;

/**
 * @brief Decode MQTT 5.0 PUBLISH packet
 *
 * @param buf Input buffer containing packet data (without fixed header)
 * @param len Length of data in buffer
 * @param flags Flags from fixed header (contains DUP, QoS, RETAIN)
 * @param out Output structure to receive parsed data
 * @return MQTT_OK on success, error code on failure
 *
 * @note Caller must free out->properties using mqtt_property_list_free()
 */
mqtt_error_t mqtt_v5_decode_publish(const uint8_t *buf, size_t len, uint8_t flags,
                                     mqtt_v5_publish_t *out);

/* ========================================================================== */
/* PUBACK/PUBREC/PUBREL/PUBCOMP Encoding/Decoding                            */
/* ========================================================================== */

/**
 * @brief MQTT 5.0 acknowledgment packet structure
 */
typedef struct mqtt_v5_ack {
    uint16_t packet_id;             /**< Packet identifier */
    mqtt_reason_code_t reason_code; /**< Reason code (0x00 if omitted) */
    mqtt_property_t *properties;    /**< Properties list (caller must free) */
} mqtt_v5_ack_t;

/**
 * @brief Encode MQTT 5.0 PUBACK packet
 */
ssize_t mqtt_v5_encode_puback(mqtt_buffer_t *buf, uint16_t packet_id,
                               mqtt_reason_code_t reason_code, mqtt_property_t *properties);

/**
 * @brief Encode MQTT 5.0 PUBREC packet
 */
ssize_t mqtt_v5_encode_pubrec(mqtt_buffer_t *buf, uint16_t packet_id,
                               mqtt_reason_code_t reason_code, mqtt_property_t *properties);

/**
 * @brief Encode MQTT 5.0 PUBREL packet
 */
ssize_t mqtt_v5_encode_pubrel(mqtt_buffer_t *buf, uint16_t packet_id,
                               mqtt_reason_code_t reason_code, mqtt_property_t *properties);

/**
 * @brief Encode MQTT 5.0 PUBCOMP packet
 */
ssize_t mqtt_v5_encode_pubcomp(mqtt_buffer_t *buf, uint16_t packet_id,
                                mqtt_reason_code_t reason_code, mqtt_property_t *properties);

/**
 * @brief Decode MQTT 5.0 acknowledgment packet (PUBACK/PUBREC/PUBREL/PUBCOMP)
 *
 * @param buf Input buffer containing packet data (without fixed header)
 * @param len Length of data in buffer
 * @param out Output structure to receive parsed data
 * @return MQTT_OK on success, error code on failure
 *
 * @note Caller must free out->properties using mqtt_property_list_free()
 */
mqtt_error_t mqtt_v5_decode_ack(const uint8_t *buf, size_t len, mqtt_v5_ack_t *out);

/* ========================================================================== */
/* SUBSCRIBE Packet Encoding                                                  */
/* ========================================================================== */

/**
 * @brief MQTT 5.0 subscription options
 */
typedef struct mqtt_v5_subscription {
    const char *topic_filter;           /**< Topic filter string */
    mqtt_qos_t max_qos;                 /**< Maximum QoS level */
    bool no_local;                      /**< No local flag */
    bool retain_as_published;           /**< Retain as published flag */
    mqtt_retain_handling_t retain_handling; /**< Retain handling option */
} mqtt_v5_subscription_t;

/**
 * @brief Encode MQTT 5.0 SUBSCRIBE packet
 *
 * @param buf Output buffer to write encoded packet
 * @param packet_id Packet identifier
 * @param subs Array of subscription entries
 * @param count Number of subscriptions
 * @param properties Optional properties list (can be NULL)
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v5_encode_subscribe(mqtt_buffer_t *buf, uint16_t packet_id,
                                  const mqtt_v5_subscription_t *subs, size_t count,
                                  mqtt_property_t *properties);

/* ========================================================================== */
/* SUBACK Decoding                                                            */
/* ========================================================================== */

/**
 * @brief MQTT 5.0 SUBACK packet structure
 */
typedef struct mqtt_v5_suback {
    uint16_t packet_id;             /**< Packet identifier from SUBSCRIBE */
    mqtt_reason_code_t *reason_codes; /**< Array of reason codes (caller must free) */
    size_t count;                   /**< Number of reason codes */
    mqtt_property_t *properties;    /**< Properties list (caller must free) */
} mqtt_v5_suback_t;

/**
 * @brief Decode MQTT 5.0 SUBACK packet
 *
 * @param buf Input buffer containing packet data (without fixed header)
 * @param len Length of data in buffer
 * @param out Output structure to receive parsed data
 * @return MQTT_OK on success, error code on failure
 *
 * @note Caller must free out->reason_codes and out->properties
 */
mqtt_error_t mqtt_v5_decode_suback(const uint8_t *buf, size_t len, mqtt_v5_suback_t *out);

/* ========================================================================== */
/* UNSUBSCRIBE Packet Encoding                                                */
/* ========================================================================== */

/**
 * @brief Encode MQTT 5.0 UNSUBSCRIBE packet
 *
 * @param buf Output buffer to write encoded packet
 * @param packet_id Packet identifier
 * @param topic_filters Array of topic filter strings
 * @param count Number of topic filters
 * @param properties Optional properties list (can be NULL)
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v5_encode_unsubscribe(mqtt_buffer_t *buf, uint16_t packet_id,
                                    const char **topic_filters, size_t count,
                                    mqtt_property_t *properties);

/* ========================================================================== */
/* UNSUBACK Decoding                                                          */
/* ========================================================================== */

/**
 * @brief MQTT 5.0 UNSUBACK packet structure
 */
typedef struct mqtt_v5_unsuback {
    uint16_t packet_id;             /**< Packet identifier from UNSUBSCRIBE */
    mqtt_reason_code_t *reason_codes; /**< Array of reason codes (caller must free) */
    size_t count;                   /**< Number of reason codes */
    mqtt_property_t *properties;    /**< Properties list (caller must free) */
} mqtt_v5_unsuback_t;

/**
 * @brief Decode MQTT 5.0 UNSUBACK packet
 */
mqtt_error_t mqtt_v5_decode_unsuback(const uint8_t *buf, size_t len, mqtt_v5_unsuback_t *out);

/* ========================================================================== */
/* DISCONNECT Packet                                                          */
/* ========================================================================== */

/**
 * @brief MQTT 5.0 DISCONNECT packet structure
 */
typedef struct mqtt_v5_disconnect {
    mqtt_reason_code_t reason_code; /**< Disconnect reason code */
    mqtt_property_t *properties;    /**< Properties list (caller must free) */
} mqtt_v5_disconnect_t;

/**
 * @brief Encode MQTT 5.0 DISCONNECT packet
 *
 * @param buf Output buffer to write encoded packet
 * @param reason_code Reason code for disconnection
 * @param properties Optional properties list (can be NULL)
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v5_encode_disconnect(mqtt_buffer_t *buf, mqtt_reason_code_t reason_code,
                                   mqtt_property_t *properties);

/**
 * @brief Decode MQTT 5.0 DISCONNECT packet
 */
mqtt_error_t mqtt_v5_decode_disconnect(const uint8_t *buf, size_t len, mqtt_v5_disconnect_t *out);

/* ========================================================================== */
/* AUTH Packet (Enhanced Authentication)                                      */
/* ========================================================================== */

/**
 * @brief MQTT 5.0 AUTH packet structure
 */
typedef struct mqtt_v5_auth {
    mqtt_reason_code_t reason_code; /**< Authentication reason code */
    mqtt_property_t *properties;    /**< Properties list (caller must free) */
} mqtt_v5_auth_t;

/**
 * @brief Encode MQTT 5.0 AUTH packet
 *
 * @param buf Output buffer to write encoded packet
 * @param reason_code Authentication reason code
 * @param properties Properties (must include Authentication Method)
 * @return Number of bytes written on success, negative error code on failure
 */
ssize_t mqtt_v5_encode_auth(mqtt_buffer_t *buf, mqtt_reason_code_t reason_code,
                             mqtt_property_t *properties);

/**
 * @brief Decode MQTT 5.0 AUTH packet
 */
mqtt_error_t mqtt_v5_decode_auth(const uint8_t *buf, size_t len, mqtt_v5_auth_t *out);

/* ========================================================================== */
/* Control Packets (PINGREQ/PINGRESP)                                        */
/* ========================================================================== */

/**
 * @brief Encode MQTT 5.0 PINGREQ packet
 */
ssize_t mqtt_v5_encode_pingreq(mqtt_buffer_t *buf);

/**
 * @brief Encode MQTT 5.0 PINGRESP packet
 */
ssize_t mqtt_v5_encode_pingresp(mqtt_buffer_t *buf);

/* ========================================================================== */
/* Utility Functions                                                          */
/* ========================================================================== */

/**
 * @brief Get reason code name string (for debugging)
 *
 * @param code Reason code
 * @return Reason code name or "Unknown"
 */
const char *mqtt_v5_reason_code_str(mqtt_reason_code_t code);

/**
 * @brief Check if reason code indicates success
 *
 * @param code Reason code
 * @return true if success (code < 0x80), false otherwise
 */
bool mqtt_v5_reason_code_success(mqtt_reason_code_t code);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_V5_H */
