/**
 * @file mqtt_v5_properties.h
 * @brief MQTT 5.0 Properties System
 *
 * This header defines the property types, structures, and functions for
 * handling MQTT 5.0 properties. Properties are key-value pairs that can
 * be attached to most MQTT 5.0 packets.
 *
 * @note This is an internal header - not part of the public API
 */

#ifndef MQTT_V5_PROPERTIES_H
#define MQTT_V5_PROPERTIES_H

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
/* Property Identifiers (MQTT 5.0 Spec Section 2.2.2)                         */
/* ========================================================================== */

/**
 * @brief MQTT 5.0 Property Identifiers
 */
typedef enum mqtt_property_id {
    /* Payload Format and Content */
    MQTT_PROP_PAYLOAD_FORMAT_INDICATOR      = 0x01,  /* Byte */
    MQTT_PROP_MESSAGE_EXPIRY_INTERVAL       = 0x02,  /* Four Byte Integer */
    MQTT_PROP_CONTENT_TYPE                  = 0x03,  /* UTF-8 String */

    /* Response and Correlation */
    MQTT_PROP_RESPONSE_TOPIC                = 0x08,  /* UTF-8 String */
    MQTT_PROP_CORRELATION_DATA              = 0x09,  /* Binary Data */

    /* Subscription */
    MQTT_PROP_SUBSCRIPTION_IDENTIFIER       = 0x0B,  /* Variable Byte Integer */

    /* Session */
    MQTT_PROP_SESSION_EXPIRY_INTERVAL       = 0x11,  /* Four Byte Integer */
    MQTT_PROP_ASSIGNED_CLIENT_IDENTIFIER    = 0x12,  /* UTF-8 String */
    MQTT_PROP_SERVER_KEEP_ALIVE             = 0x13,  /* Two Byte Integer */

    /* Authentication */
    MQTT_PROP_AUTHENTICATION_METHOD         = 0x15,  /* UTF-8 String */
    MQTT_PROP_AUTHENTICATION_DATA           = 0x16,  /* Binary Data */

    /* Request/Response Information */
    MQTT_PROP_REQUEST_PROBLEM_INFORMATION   = 0x17,  /* Byte */
    MQTT_PROP_WILL_DELAY_INTERVAL           = 0x18,  /* Four Byte Integer */
    MQTT_PROP_REQUEST_RESPONSE_INFORMATION  = 0x19,  /* Byte */
    MQTT_PROP_RESPONSE_INFORMATION          = 0x1A,  /* UTF-8 String */

    /* Server Reference */
    MQTT_PROP_SERVER_REFERENCE              = 0x1C,  /* UTF-8 String */

    /* Reason and Status */
    MQTT_PROP_REASON_STRING                 = 0x1F,  /* UTF-8 String */

    /* Flow Control */
    MQTT_PROP_RECEIVE_MAXIMUM               = 0x21,  /* Two Byte Integer */
    MQTT_PROP_TOPIC_ALIAS_MAXIMUM           = 0x22,  /* Two Byte Integer */
    MQTT_PROP_TOPIC_ALIAS                   = 0x23,  /* Two Byte Integer */
    MQTT_PROP_MAXIMUM_QOS                   = 0x24,  /* Byte */
    MQTT_PROP_RETAIN_AVAILABLE              = 0x25,  /* Byte */

    /* User Properties */
    MQTT_PROP_USER_PROPERTY                 = 0x26,  /* UTF-8 String Pair */

    /* Packet Size */
    MQTT_PROP_MAXIMUM_PACKET_SIZE           = 0x27,  /* Four Byte Integer */

    /* Feature Availability */
    MQTT_PROP_WILDCARD_SUBSCRIPTION_AVAILABLE   = 0x28,  /* Byte */
    MQTT_PROP_SUBSCRIPTION_IDENTIFIER_AVAILABLE = 0x29,  /* Byte */
    MQTT_PROP_SHARED_SUBSCRIPTION_AVAILABLE     = 0x2A   /* Byte */
} mqtt_property_id_t;

/* ========================================================================== */
/* Property Data Types                                                        */
/* ========================================================================== */

/**
 * @brief Property data type
 */
typedef enum mqtt_property_type {
    MQTT_PROP_TYPE_BYTE,              /* Single byte (0-255) */
    MQTT_PROP_TYPE_TWO_BYTE_INT,      /* Two byte integer (big-endian) */
    MQTT_PROP_TYPE_FOUR_BYTE_INT,     /* Four byte integer (big-endian) */
    MQTT_PROP_TYPE_VARIABLE_INT,      /* Variable byte integer (1-4 bytes) */
    MQTT_PROP_TYPE_BINARY_DATA,       /* Binary data with 2-byte length prefix */
    MQTT_PROP_TYPE_UTF8_STRING,       /* UTF-8 string with 2-byte length prefix */
    MQTT_PROP_TYPE_UTF8_STRING_PAIR   /* Two UTF-8 strings (key-value) */
} mqtt_property_type_t;

/* ========================================================================== */
/* Property Value Union                                                       */
/* ========================================================================== */

/**
 * @brief Binary data or string value
 */
typedef struct mqtt_binary_data {
    uint8_t *data;      /**< Data pointer (owned by property) */
    uint16_t len;       /**< Data length */
} mqtt_binary_data_t;

/**
 * @brief String pair for user properties
 */
typedef struct mqtt_string_pair {
    char *key;          /**< Key string (null-terminated, owned) */
    char *value;        /**< Value string (null-terminated, owned) */
} mqtt_string_pair_t;

/**
 * @brief Property value union
 */
typedef union mqtt_property_value {
    uint8_t byte;                   /**< Byte value */
    uint16_t u16;                   /**< Two-byte integer */
    uint32_t u32;                   /**< Four-byte integer / variable int */
    mqtt_binary_data_t binary;      /**< Binary data */
    char *str;                      /**< UTF-8 string (null-terminated, owned) */
    mqtt_string_pair_t string_pair; /**< String pair (for user properties) */
} mqtt_property_value_t;

/* ========================================================================== */
/* Property Structure (Linked List Node)                                      */
/* ========================================================================== */

/**
 * @brief MQTT 5.0 Property (linked list node)
 *
 * Properties are stored as a linked list to allow multiple properties
 * of the same type (e.g., multiple user properties).
 */
struct mqtt_property {
    mqtt_property_id_t id;          /**< Property identifier */
    mqtt_property_type_t type;      /**< Data type */
    mqtt_property_value_t value;    /**< Property value */
    struct mqtt_property *next;     /**< Next property in list */
};

/* ========================================================================== */
/* Property List Management                                                   */
/* ========================================================================== */

/**
 * @brief Create a new property
 *
 * @param id Property identifier (use mqtt_property_id_t values)
 * @return New property or NULL on error
 */
mqtt_property_t *mqtt_property_create(uint8_t id);

/**
 * @brief Free a single property
 *
 * @param prop Property to free
 */
void mqtt_property_free(mqtt_property_t *prop);

/**
 * @brief Free entire property list
 *
 * @param list Head of property list
 */
void mqtt_property_list_free(mqtt_property_t *list);

/**
 * @brief Append property to list
 *
 * @param list Pointer to list head pointer
 * @param prop Property to append
 */
void mqtt_property_list_append(mqtt_property_t **list, mqtt_property_t *prop);

/**
 * @brief Find property by ID in list
 *
 * @param list Property list head
 * @param id Property ID to find
 * @return First matching property or NULL
 */
mqtt_property_t *mqtt_property_list_find(mqtt_property_t *list, mqtt_property_id_t id);

/**
 * @brief Count properties in list
 *
 * @param list Property list head
 * @return Number of properties
 */
size_t mqtt_property_list_count(mqtt_property_t *list);

/* ========================================================================== */
/* Property Creation Helpers                                                  */
/* ========================================================================== */

/**
 * @brief Add byte property to list
 */
mqtt_error_t mqtt_property_add_byte(mqtt_property_t **list, mqtt_property_id_t id, uint8_t value);

/**
 * @brief Add two-byte integer property to list
 */
mqtt_error_t mqtt_property_add_u16(mqtt_property_t **list, mqtt_property_id_t id, uint16_t value);

/**
 * @brief Add four-byte integer property to list
 */
mqtt_error_t mqtt_property_add_u32(mqtt_property_t **list, mqtt_property_id_t id, uint32_t value);

/**
 * @brief Add variable byte integer property to list
 */
mqtt_error_t mqtt_property_add_varint(mqtt_property_t **list, mqtt_property_id_t id, uint32_t value);

/**
 * @brief Add UTF-8 string property to list (copies string)
 */
mqtt_error_t mqtt_property_add_string(mqtt_property_t **list, mqtt_property_id_t id, const char *str);

/**
 * @brief Add binary data property to list (copies data)
 */
mqtt_error_t mqtt_property_add_binary(mqtt_property_t **list, mqtt_property_id_t id,
                                       const uint8_t *data, uint16_t len);

/**
 * @brief Add user property (string pair) to list
 */
mqtt_error_t mqtt_property_add_user_property(mqtt_property_t **list,
                                              const char *key, const char *value);

/* ========================================================================== */
/* Property Encoding/Decoding                                                 */
/* ========================================================================== */

/**
 * @brief Calculate encoded size of property list
 *
 * @param list Property list head
 * @return Total encoded size in bytes (including property length field)
 */
size_t mqtt_property_encoded_size(mqtt_property_t *list);

/**
 * @brief Encode property list to buffer
 *
 * Writes the property length as a variable byte integer followed by
 * all property ID-value pairs.
 *
 * @param buf Output buffer
 * @param list Property list to encode
 * @return Number of bytes written, or negative error code
 */
ssize_t mqtt_property_encode(mqtt_buffer_t *buf, mqtt_property_t *list);

/**
 * @brief Decode property list from buffer
 *
 * Reads property length and all properties from the buffer.
 *
 * @param data Input data buffer
 * @param len Available data length
 * @param list Output pointer to receive property list head
 * @param bytes_read Output: number of bytes consumed
 * @return MQTT_OK on success, error code on failure
 */
mqtt_error_t mqtt_property_decode(const uint8_t *data, size_t len,
                                   mqtt_property_t **list, size_t *bytes_read);

/* ========================================================================== */
/* Property Type Information                                                  */
/* ========================================================================== */

/**
 * @brief Get data type for a property ID
 *
 * @param id Property identifier
 * @return Property data type
 */
mqtt_property_type_t mqtt_property_get_type(mqtt_property_id_t id);

/**
 * @brief Check if property ID is valid
 *
 * @param id Property identifier
 * @return true if valid, false otherwise
 */
bool mqtt_property_id_valid(mqtt_property_id_t id);

/**
 * @brief Get property name string (for debugging)
 *
 * @param id Property identifier
 * @return Property name or "Unknown"
 */
const char *mqtt_property_name(mqtt_property_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_V5_PROPERTIES_H */
