/**
 * @file mqtt_v5_properties.c
 * @brief MQTT 5.0 Properties System Implementation
 *
 * Implements property list management, encoding, and decoding for MQTT 5.0.
 */

#define _POSIX_C_SOURCE 200809L  /* For strdup */

#include "mqtt_v5_properties.h"
#include "../../core/mqtt_varint.h"
#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/* Property Type Mapping                                                      */
/* ========================================================================== */

/**
 * @brief Property type lookup table
 */
static const struct {
    mqtt_property_id_t id;
    mqtt_property_type_t type;
    const char *name;
} property_info[] = {
    { MQTT_PROP_PAYLOAD_FORMAT_INDICATOR,       MQTT_PROP_TYPE_BYTE,            "Payload Format Indicator" },
    { MQTT_PROP_MESSAGE_EXPIRY_INTERVAL,        MQTT_PROP_TYPE_FOUR_BYTE_INT,   "Message Expiry Interval" },
    { MQTT_PROP_CONTENT_TYPE,                   MQTT_PROP_TYPE_UTF8_STRING,     "Content Type" },
    { MQTT_PROP_RESPONSE_TOPIC,                 MQTT_PROP_TYPE_UTF8_STRING,     "Response Topic" },
    { MQTT_PROP_CORRELATION_DATA,               MQTT_PROP_TYPE_BINARY_DATA,     "Correlation Data" },
    { MQTT_PROP_SUBSCRIPTION_IDENTIFIER,        MQTT_PROP_TYPE_VARIABLE_INT,    "Subscription Identifier" },
    { MQTT_PROP_SESSION_EXPIRY_INTERVAL,        MQTT_PROP_TYPE_FOUR_BYTE_INT,   "Session Expiry Interval" },
    { MQTT_PROP_ASSIGNED_CLIENT_IDENTIFIER,     MQTT_PROP_TYPE_UTF8_STRING,     "Assigned Client Identifier" },
    { MQTT_PROP_SERVER_KEEP_ALIVE,              MQTT_PROP_TYPE_TWO_BYTE_INT,    "Server Keep Alive" },
    { MQTT_PROP_AUTHENTICATION_METHOD,          MQTT_PROP_TYPE_UTF8_STRING,     "Authentication Method" },
    { MQTT_PROP_AUTHENTICATION_DATA,            MQTT_PROP_TYPE_BINARY_DATA,     "Authentication Data" },
    { MQTT_PROP_REQUEST_PROBLEM_INFORMATION,    MQTT_PROP_TYPE_BYTE,            "Request Problem Information" },
    { MQTT_PROP_WILL_DELAY_INTERVAL,            MQTT_PROP_TYPE_FOUR_BYTE_INT,   "Will Delay Interval" },
    { MQTT_PROP_REQUEST_RESPONSE_INFORMATION,   MQTT_PROP_TYPE_BYTE,            "Request Response Information" },
    { MQTT_PROP_RESPONSE_INFORMATION,           MQTT_PROP_TYPE_UTF8_STRING,     "Response Information" },
    { MQTT_PROP_SERVER_REFERENCE,               MQTT_PROP_TYPE_UTF8_STRING,     "Server Reference" },
    { MQTT_PROP_REASON_STRING,                  MQTT_PROP_TYPE_UTF8_STRING,     "Reason String" },
    { MQTT_PROP_RECEIVE_MAXIMUM,                MQTT_PROP_TYPE_TWO_BYTE_INT,    "Receive Maximum" },
    { MQTT_PROP_TOPIC_ALIAS_MAXIMUM,            MQTT_PROP_TYPE_TWO_BYTE_INT,    "Topic Alias Maximum" },
    { MQTT_PROP_TOPIC_ALIAS,                    MQTT_PROP_TYPE_TWO_BYTE_INT,    "Topic Alias" },
    { MQTT_PROP_MAXIMUM_QOS,                    MQTT_PROP_TYPE_BYTE,            "Maximum QoS" },
    { MQTT_PROP_RETAIN_AVAILABLE,               MQTT_PROP_TYPE_BYTE,            "Retain Available" },
    { MQTT_PROP_USER_PROPERTY,                  MQTT_PROP_TYPE_UTF8_STRING_PAIR,"User Property" },
    { MQTT_PROP_MAXIMUM_PACKET_SIZE,            MQTT_PROP_TYPE_FOUR_BYTE_INT,   "Maximum Packet Size" },
    { MQTT_PROP_WILDCARD_SUBSCRIPTION_AVAILABLE,    MQTT_PROP_TYPE_BYTE,        "Wildcard Subscription Available" },
    { MQTT_PROP_SUBSCRIPTION_IDENTIFIER_AVAILABLE,  MQTT_PROP_TYPE_BYTE,        "Subscription Identifier Available" },
    { MQTT_PROP_SHARED_SUBSCRIPTION_AVAILABLE,      MQTT_PROP_TYPE_BYTE,        "Shared Subscription Available" },
};

#define PROPERTY_INFO_COUNT (sizeof(property_info) / sizeof(property_info[0]))

/* ========================================================================== */
/* Property Type Information                                                  */
/* ========================================================================== */

mqtt_property_type_t mqtt_property_get_type(mqtt_property_id_t id)
{
    for (size_t i = 0; i < PROPERTY_INFO_COUNT; i++) {
        if (property_info[i].id == id) {
            return property_info[i].type;
        }
    }
    return MQTT_PROP_TYPE_BYTE; /* Default fallback */
}

bool mqtt_property_id_valid(mqtt_property_id_t id)
{
    for (size_t i = 0; i < PROPERTY_INFO_COUNT; i++) {
        if (property_info[i].id == id) {
            return true;
        }
    }
    return false;
}

const char *mqtt_property_name(mqtt_property_id_t id)
{
    for (size_t i = 0; i < PROPERTY_INFO_COUNT; i++) {
        if (property_info[i].id == id) {
            return property_info[i].name;
        }
    }
    return "Unknown";
}

/* ========================================================================== */
/* Property Creation and Management                                           */
/* ========================================================================== */

mqtt_property_t *mqtt_property_create(uint8_t id)
{
    mqtt_property_t *prop = calloc(1, sizeof(mqtt_property_t));
    if (prop == NULL) {
        return NULL;
    }

    prop->id = (mqtt_property_id_t)id;
    prop->type = mqtt_property_get_type((mqtt_property_id_t)id);
    prop->next = NULL;

    return prop;
}

void mqtt_property_free(mqtt_property_t *prop)
{
    if (prop == NULL) {
        return;
    }

    /* Free any allocated data based on type */
    switch (prop->type) {
        case MQTT_PROP_TYPE_UTF8_STRING:
            free(prop->value.str);
            break;
        case MQTT_PROP_TYPE_BINARY_DATA:
            free(prop->value.binary.data);
            break;
        case MQTT_PROP_TYPE_UTF8_STRING_PAIR:
            free(prop->value.string_pair.key);
            free(prop->value.string_pair.value);
            break;
        default:
            break;
    }

    free(prop);
}

void mqtt_property_list_free(mqtt_property_t *list)
{
    while (list != NULL) {
        mqtt_property_t *next = list->next;
        mqtt_property_free(list);
        list = next;
    }
}

void mqtt_property_list_append(mqtt_property_t **list, mqtt_property_t *prop)
{
    if (list == NULL || prop == NULL) {
        return;
    }

    if (*list == NULL) {
        *list = prop;
    } else {
        mqtt_property_t *tail = *list;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = prop;
    }
}

mqtt_property_t *mqtt_property_list_find(mqtt_property_t *list, mqtt_property_id_t id)
{
    while (list != NULL) {
        if (list->id == id) {
            return list;
        }
        list = list->next;
    }
    return NULL;
}

size_t mqtt_property_list_count(mqtt_property_t *list)
{
    size_t count = 0;
    while (list != NULL) {
        count++;
        list = list->next;
    }
    return count;
}

/* ========================================================================== */
/* Property Creation Helpers                                                  */
/* ========================================================================== */

mqtt_error_t mqtt_property_add_byte(mqtt_property_t **list, mqtt_property_id_t id, uint8_t value)
{
    mqtt_property_t *prop = mqtt_property_create(id);
    if (prop == NULL) {
        return MQTT_ERR_NOMEM;
    }

    prop->type = MQTT_PROP_TYPE_BYTE;
    prop->value.byte = value;
    mqtt_property_list_append(list, prop);
    return MQTT_OK;
}

mqtt_error_t mqtt_property_add_u16(mqtt_property_t **list, mqtt_property_id_t id, uint16_t value)
{
    mqtt_property_t *prop = mqtt_property_create(id);
    if (prop == NULL) {
        return MQTT_ERR_NOMEM;
    }

    prop->type = MQTT_PROP_TYPE_TWO_BYTE_INT;
    prop->value.u16 = value;
    mqtt_property_list_append(list, prop);
    return MQTT_OK;
}

mqtt_error_t mqtt_property_add_u32(mqtt_property_t **list, mqtt_property_id_t id, uint32_t value)
{
    mqtt_property_t *prop = mqtt_property_create(id);
    if (prop == NULL) {
        return MQTT_ERR_NOMEM;
    }

    prop->type = MQTT_PROP_TYPE_FOUR_BYTE_INT;
    prop->value.u32 = value;
    mqtt_property_list_append(list, prop);
    return MQTT_OK;
}

mqtt_error_t mqtt_property_add_varint(mqtt_property_t **list, mqtt_property_id_t id, uint32_t value)
{
    mqtt_property_t *prop = mqtt_property_create(id);
    if (prop == NULL) {
        return MQTT_ERR_NOMEM;
    }

    prop->type = MQTT_PROP_TYPE_VARIABLE_INT;
    prop->value.u32 = value;
    mqtt_property_list_append(list, prop);
    return MQTT_OK;
}

mqtt_error_t mqtt_property_add_string(mqtt_property_t **list, mqtt_property_id_t id, const char *str)
{
    if (str == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_property_t *prop = mqtt_property_create(id);
    if (prop == NULL) {
        return MQTT_ERR_NOMEM;
    }

    prop->type = MQTT_PROP_TYPE_UTF8_STRING;
    prop->value.str = strdup(str);
    if (prop->value.str == NULL) {
        mqtt_property_free(prop);
        return MQTT_ERR_NOMEM;
    }

    mqtt_property_list_append(list, prop);
    return MQTT_OK;
}

mqtt_error_t mqtt_property_add_binary(mqtt_property_t **list, mqtt_property_id_t id,
                                       const uint8_t *data, uint16_t len)
{
    mqtt_property_t *prop = mqtt_property_create(id);
    if (prop == NULL) {
        return MQTT_ERR_NOMEM;
    }

    prop->type = MQTT_PROP_TYPE_BINARY_DATA;
    if (len > 0 && data != NULL) {
        prop->value.binary.data = malloc(len);
        if (prop->value.binary.data == NULL) {
            mqtt_property_free(prop);
            return MQTT_ERR_NOMEM;
        }
        memcpy(prop->value.binary.data, data, len);
        prop->value.binary.len = len;
    } else {
        prop->value.binary.data = NULL;
        prop->value.binary.len = 0;
    }

    mqtt_property_list_append(list, prop);
    return MQTT_OK;
}

mqtt_error_t mqtt_property_add_user_property(mqtt_property_t **list,
                                              const char *key, const char *value)
{
    if (key == NULL || value == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    mqtt_property_t *prop = mqtt_property_create(MQTT_PROP_USER_PROPERTY);
    if (prop == NULL) {
        return MQTT_ERR_NOMEM;
    }

    prop->type = MQTT_PROP_TYPE_UTF8_STRING_PAIR;
    prop->value.string_pair.key = strdup(key);
    prop->value.string_pair.value = strdup(value);

    if (prop->value.string_pair.key == NULL || prop->value.string_pair.value == NULL) {
        mqtt_property_free(prop);
        return MQTT_ERR_NOMEM;
    }

    mqtt_property_list_append(list, prop);
    return MQTT_OK;
}

/* ========================================================================== */
/* Property Encoding                                                          */
/* ========================================================================== */

/**
 * @brief Calculate size of a single property value
 */
static size_t property_value_size(mqtt_property_t *prop)
{
    size_t size = 1; /* Property ID (1 byte) */

    switch (prop->type) {
        case MQTT_PROP_TYPE_BYTE:
            size += 1;
            break;
        case MQTT_PROP_TYPE_TWO_BYTE_INT:
            size += 2;
            break;
        case MQTT_PROP_TYPE_FOUR_BYTE_INT:
            size += 4;
            break;
        case MQTT_PROP_TYPE_VARIABLE_INT: {
            int vsize = mqtt_varint_size(prop->value.u32);
            size += (vsize > 0) ? (size_t)vsize : 1;
            break;
        }
        case MQTT_PROP_TYPE_UTF8_STRING:
            size += 2 + (prop->value.str ? strlen(prop->value.str) : 0);
            break;
        case MQTT_PROP_TYPE_BINARY_DATA:
            size += 2 + prop->value.binary.len;
            break;
        case MQTT_PROP_TYPE_UTF8_STRING_PAIR:
            size += 2 + (prop->value.string_pair.key ? strlen(prop->value.string_pair.key) : 0);
            size += 2 + (prop->value.string_pair.value ? strlen(prop->value.string_pair.value) : 0);
            break;
    }

    return size;
}

size_t mqtt_property_encoded_size(mqtt_property_t *list)
{
    size_t content_size = 0;

    /* Calculate total size of all properties */
    while (list != NULL) {
        content_size += property_value_size(list);
        list = list->next;
    }

    /* Add size of the property length field (variable byte integer) */
    int vsize = mqtt_varint_size((uint32_t)content_size);
    return ((vsize > 0) ? (size_t)vsize : 1) + content_size;
}

/**
 * @brief Encode a single property to buffer
 */
static ssize_t encode_property(mqtt_buffer_t *buf, mqtt_property_t *prop)
{
    size_t start = mqtt_buffer_len(buf);
    uint8_t *ptr;
    size_t len;

    /* Reserve space and get write pointer */
    size_t needed = property_value_size(prop);
    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + needed) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }

    ptr = mqtt_buffer_write_ptr(buf);

    /* Write property ID */
    *ptr++ = (uint8_t)prop->id;

    /* Write property value based on type */
    switch (prop->type) {
        case MQTT_PROP_TYPE_BYTE:
            *ptr++ = prop->value.byte;
            break;

        case MQTT_PROP_TYPE_TWO_BYTE_INT:
            *ptr++ = (prop->value.u16 >> 8) & 0xFF;
            *ptr++ = prop->value.u16 & 0xFF;
            break;

        case MQTT_PROP_TYPE_FOUR_BYTE_INT:
            *ptr++ = (prop->value.u32 >> 24) & 0xFF;
            *ptr++ = (prop->value.u32 >> 16) & 0xFF;
            *ptr++ = (prop->value.u32 >> 8) & 0xFF;
            *ptr++ = prop->value.u32 & 0xFF;
            break;

        case MQTT_PROP_TYPE_VARIABLE_INT: {
            int varint_len = mqtt_varint_encode(prop->value.u32, ptr);
            if (varint_len > 0) {
                ptr += varint_len;
            }
            break;
        }

        case MQTT_PROP_TYPE_UTF8_STRING:
            len = prop->value.str ? strlen(prop->value.str) : 0;
            *ptr++ = (len >> 8) & 0xFF;
            *ptr++ = len & 0xFF;
            if (len > 0) {
                memcpy(ptr, prop->value.str, len);
                ptr += len;
            }
            break;

        case MQTT_PROP_TYPE_BINARY_DATA:
            *ptr++ = (prop->value.binary.len >> 8) & 0xFF;
            *ptr++ = prop->value.binary.len & 0xFF;
            if (prop->value.binary.len > 0 && prop->value.binary.data) {
                memcpy(ptr, prop->value.binary.data, prop->value.binary.len);
                ptr += prop->value.binary.len;
            }
            break;

        case MQTT_PROP_TYPE_UTF8_STRING_PAIR:
            /* Key */
            len = prop->value.string_pair.key ? strlen(prop->value.string_pair.key) : 0;
            *ptr++ = (len >> 8) & 0xFF;
            *ptr++ = len & 0xFF;
            if (len > 0) {
                memcpy(ptr, prop->value.string_pair.key, len);
                ptr += len;
            }
            /* Value */
            len = prop->value.string_pair.value ? strlen(prop->value.string_pair.value) : 0;
            *ptr++ = (len >> 8) & 0xFF;
            *ptr++ = len & 0xFF;
            if (len > 0) {
                memcpy(ptr, prop->value.string_pair.value, len);
                ptr += len;
            }
            break;
    }

    /* Advance buffer write position */
    mqtt_buffer_advance_write(buf, needed);

    return (ssize_t)(mqtt_buffer_len(buf) - start);
}

ssize_t mqtt_property_encode(mqtt_buffer_t *buf, mqtt_property_t *list)
{
    size_t start = mqtt_buffer_len(buf);

    /* Calculate content size */
    size_t content_size = 0;
    mqtt_property_t *prop = list;
    while (prop != NULL) {
        content_size += property_value_size(prop);
        prop = prop->next;
    }

    /* Write property length as variable byte integer */
    int varint_len = mqtt_varint_size((uint32_t)content_size);
    if (varint_len < 0) {
        return MQTT_ERR_PACKET_TOO_LARGE;
    }
    size_t varint_size = (size_t)varint_len;
    if (mqtt_buffer_reserve(buf, mqtt_buffer_len(buf) + varint_size) != MQTT_OK) {
        return MQTT_ERR_NOMEM;
    }
    uint8_t *ptr = mqtt_buffer_write_ptr(buf);
    mqtt_varint_encode((uint32_t)content_size, ptr);
    mqtt_buffer_advance_write(buf, varint_size);

    /* Encode each property */
    prop = list;
    while (prop != NULL) {
        ssize_t written = encode_property(buf, prop);
        if (written < 0) {
            return written;
        }
        prop = prop->next;
    }

    return (ssize_t)(mqtt_buffer_len(buf) - start);
}

/* ========================================================================== */
/* Property Decoding                                                          */
/* ========================================================================== */

/**
 * @brief Read a UTF-8 string from buffer
 */
static mqtt_error_t read_utf8_string(const uint8_t *data, size_t len,
                                      char **out, size_t *bytes_read)
{
    if (len < 2) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    uint16_t str_len = ((uint16_t)data[0] << 8) | data[1];
    if (len < 2u + str_len) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    *out = malloc((size_t)str_len + 1);
    if (*out == NULL) {
        return MQTT_ERR_NOMEM;
    }

    memcpy(*out, data + 2, str_len);
    (*out)[str_len] = '\0';
    *bytes_read = 2u + str_len;

    return MQTT_OK;
}

/**
 * @brief Read binary data from buffer
 */
static mqtt_error_t read_binary_data(const uint8_t *data, size_t len,
                                      uint8_t **out, uint16_t *out_len, size_t *bytes_read)
{
    if (len < 2) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    uint16_t bin_len = ((uint16_t)data[0] << 8) | data[1];
    if (len < 2u + bin_len) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    if (bin_len > 0) {
        *out = malloc(bin_len);
        if (*out == NULL) {
            return MQTT_ERR_NOMEM;
        }
        memcpy(*out, data + 2, bin_len);
    } else {
        *out = NULL;
    }

    *out_len = bin_len;
    *bytes_read = 2u + bin_len;

    return MQTT_OK;
}

mqtt_error_t mqtt_property_decode(const uint8_t *data, size_t len,
                                   mqtt_property_t **list, size_t *bytes_read)
{
    if (data == NULL || list == NULL || bytes_read == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    *list = NULL;
    *bytes_read = 0;

    /* Read property length (variable byte integer) */
    uint32_t prop_len;
    int varint_result = mqtt_varint_decode(data, len, &prop_len);
    if (varint_result < 0) {
        return MQTT_ERR_MALFORMED_PACKET;
    }
    size_t varint_bytes = (size_t)varint_result;

    data += varint_bytes;
    len -= varint_bytes;
    *bytes_read = varint_bytes;

    if (len < prop_len) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    /* Parse properties */
    size_t remaining = prop_len;
    while (remaining > 0) {
        /* Read property ID */
        if (remaining < 1) {
            mqtt_property_list_free(*list);
            *list = NULL;
            return MQTT_ERR_MALFORMED_PACKET;
        }

        mqtt_property_id_t id = (mqtt_property_id_t)*data++;
        remaining--;

        if (!mqtt_property_id_valid(id)) {
            mqtt_property_list_free(*list);
            *list = NULL;
            return MQTT_ERR_PROTOCOL;
        }

        mqtt_property_t *prop = mqtt_property_create(id);
        if (prop == NULL) {
            mqtt_property_list_free(*list);
            *list = NULL;
            return MQTT_ERR_NOMEM;
        }

        /* Read property value based on type */
        size_t value_bytes = 0;
        mqtt_error_t err;
        switch (prop->type) {
            case MQTT_PROP_TYPE_BYTE:
                if (remaining < 1) {
                    mqtt_property_free(prop);
                    mqtt_property_list_free(*list);
                    *list = NULL;
                    return MQTT_ERR_MALFORMED_PACKET;
                }
                prop->value.byte = *data++;
                remaining--;
                break;

            case MQTT_PROP_TYPE_TWO_BYTE_INT:
                if (remaining < 2) {
                    mqtt_property_free(prop);
                    mqtt_property_list_free(*list);
                    *list = NULL;
                    return MQTT_ERR_MALFORMED_PACKET;
                }
                prop->value.u16 = ((uint16_t)data[0] << 8) | data[1];
                data += 2;
                remaining -= 2;
                break;

            case MQTT_PROP_TYPE_FOUR_BYTE_INT:
                if (remaining < 4) {
                    mqtt_property_free(prop);
                    mqtt_property_list_free(*list);
                    *list = NULL;
                    return MQTT_ERR_MALFORMED_PACKET;
                }
                prop->value.u32 = ((uint32_t)data[0] << 24) |
                                  ((uint32_t)data[1] << 16) |
                                  ((uint32_t)data[2] << 8) |
                                  data[3];
                data += 4;
                remaining -= 4;
                break;

            case MQTT_PROP_TYPE_VARIABLE_INT: {
                int vresult = mqtt_varint_decode(data, remaining, &prop->value.u32);
                if (vresult < 0) {
                    mqtt_property_free(prop);
                    mqtt_property_list_free(*list);
                    *list = NULL;
                    return MQTT_ERR_MALFORMED_PACKET;
                }
                value_bytes = (size_t)vresult;
                data += value_bytes;
                remaining -= value_bytes;
                break;
            }

            case MQTT_PROP_TYPE_UTF8_STRING:
                err = read_utf8_string(data, remaining, &prop->value.str, &value_bytes);
                if (err != MQTT_OK) {
                    mqtt_property_free(prop);
                    mqtt_property_list_free(*list);
                    *list = NULL;
                    return err;
                }
                data += value_bytes;
                remaining -= value_bytes;
                break;

            case MQTT_PROP_TYPE_BINARY_DATA:
                err = read_binary_data(data, remaining, &prop->value.binary.data,
                                       &prop->value.binary.len, &value_bytes);
                if (err != MQTT_OK) {
                    mqtt_property_free(prop);
                    mqtt_property_list_free(*list);
                    *list = NULL;
                    return err;
                }
                data += value_bytes;
                remaining -= value_bytes;
                break;

            case MQTT_PROP_TYPE_UTF8_STRING_PAIR:
                err = read_utf8_string(data, remaining, &prop->value.string_pair.key, &value_bytes);
                if (err != MQTT_OK) {
                    mqtt_property_free(prop);
                    mqtt_property_list_free(*list);
                    *list = NULL;
                    return err;
                }
                data += value_bytes;
                remaining -= value_bytes;

                err = read_utf8_string(data, remaining, &prop->value.string_pair.value, &value_bytes);
                if (err != MQTT_OK) {
                    mqtt_property_free(prop);
                    mqtt_property_list_free(*list);
                    *list = NULL;
                    return err;
                }
                data += value_bytes;
                remaining -= value_bytes;
                break;
        }

        mqtt_property_list_append(list, prop);
    }

    *bytes_read += prop_len;
    return MQTT_OK;
}
