/**
 * @file test_mqtt_v5_properties.c
 * @brief Unit tests for MQTT 5.0 Properties system
 */

#include "test_framework.h"
#include "protocol/mqtt_v5/mqtt_v5_properties.h"
#include "memory/mqtt_buffer.h"
#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/* Test: Property Creation                                                    */
/* ========================================================================== */

TEST(create_byte_property)
{
    mqtt_property_t *prop = mqtt_property_create(MQTT_PROP_PAYLOAD_FORMAT_INDICATOR);
    ASSERT_NOT_NULL(prop);
    ASSERT_EQ(MQTT_PROP_PAYLOAD_FORMAT_INDICATOR, prop->id);
    ASSERT_EQ(MQTT_PROP_TYPE_BYTE, prop->type);
    ASSERT_NULL(prop->next);
    mqtt_property_free(prop);
}

TEST(create_u16_property)
{
    mqtt_property_t *prop = mqtt_property_create(MQTT_PROP_SERVER_KEEP_ALIVE);
    ASSERT_NOT_NULL(prop);
    ASSERT_EQ(MQTT_PROP_SERVER_KEEP_ALIVE, prop->id);
    ASSERT_EQ(MQTT_PROP_TYPE_TWO_BYTE_INT, prop->type);
    mqtt_property_free(prop);
}

TEST(create_u32_property)
{
    mqtt_property_t *prop = mqtt_property_create(MQTT_PROP_SESSION_EXPIRY_INTERVAL);
    ASSERT_NOT_NULL(prop);
    ASSERT_EQ(MQTT_PROP_SESSION_EXPIRY_INTERVAL, prop->id);
    ASSERT_EQ(MQTT_PROP_TYPE_FOUR_BYTE_INT, prop->type);
    mqtt_property_free(prop);
}

TEST(create_string_property)
{
    mqtt_property_t *prop = mqtt_property_create(MQTT_PROP_CONTENT_TYPE);
    ASSERT_NOT_NULL(prop);
    ASSERT_EQ(MQTT_PROP_CONTENT_TYPE, prop->id);
    ASSERT_EQ(MQTT_PROP_TYPE_UTF8_STRING, prop->type);
    mqtt_property_free(prop);
}

TEST(create_binary_property)
{
    mqtt_property_t *prop = mqtt_property_create(MQTT_PROP_CORRELATION_DATA);
    ASSERT_NOT_NULL(prop);
    ASSERT_EQ(MQTT_PROP_CORRELATION_DATA, prop->id);
    ASSERT_EQ(MQTT_PROP_TYPE_BINARY_DATA, prop->type);
    mqtt_property_free(prop);
}

TEST(create_string_pair_property)
{
    mqtt_property_t *prop = mqtt_property_create(MQTT_PROP_USER_PROPERTY);
    ASSERT_NOT_NULL(prop);
    ASSERT_EQ(MQTT_PROP_USER_PROPERTY, prop->id);
    ASSERT_EQ(MQTT_PROP_TYPE_UTF8_STRING_PAIR, prop->type);
    mqtt_property_free(prop);
}

/* ========================================================================== */
/* Test: Property List Operations                                             */
/* ========================================================================== */

TEST(list_append)
{
    mqtt_property_t *list = NULL;

    mqtt_error_t err = mqtt_property_add_byte(&list, MQTT_PROP_PAYLOAD_FORMAT_INDICATOR, 1);
    ASSERT_EQ(MQTT_OK, err);
    ASSERT_NOT_NULL(list);

    err = mqtt_property_add_u16(&list, MQTT_PROP_RECEIVE_MAXIMUM, 100);
    ASSERT_EQ(MQTT_OK, err);

    /* Verify list has two items */
    ASSERT_EQ(2, mqtt_property_list_count(list));

    mqtt_property_list_free(list);
}

TEST(list_find)
{
    mqtt_property_t *list = NULL;

    mqtt_property_add_byte(&list, MQTT_PROP_PAYLOAD_FORMAT_INDICATOR, 1);
    mqtt_property_add_u16(&list, MQTT_PROP_RECEIVE_MAXIMUM, 100);
    mqtt_property_add_string(&list, MQTT_PROP_CONTENT_TYPE, "application/json");

    /* Find existing property */
    mqtt_property_t *found = mqtt_property_list_find(list, MQTT_PROP_RECEIVE_MAXIMUM);
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(100, found->value.u16);

    /* Find non-existing property */
    found = mqtt_property_list_find(list, MQTT_PROP_TOPIC_ALIAS);
    ASSERT_NULL(found);

    mqtt_property_list_free(list);
}

TEST(add_user_property)
{
    mqtt_property_t *list = NULL;

    mqtt_error_t err = mqtt_property_add_user_property(&list, "key1", "value1");
    ASSERT_EQ(MQTT_OK, err);

    err = mqtt_property_add_user_property(&list, "key2", "value2");
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_EQ(2, mqtt_property_list_count(list));

    mqtt_property_t *prop = list;
    ASSERT_STR_EQ("key1", prop->value.string_pair.key);
    ASSERT_STR_EQ("value1", prop->value.string_pair.value);

    prop = prop->next;
    ASSERT_STR_EQ("key2", prop->value.string_pair.key);
    ASSERT_STR_EQ("value2", prop->value.string_pair.value);

    mqtt_property_list_free(list);
}

TEST(add_binary_property)
{
    mqtt_property_t *list = NULL;
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};

    mqtt_error_t err = mqtt_property_add_binary(&list, MQTT_PROP_CORRELATION_DATA, data, sizeof(data));
    ASSERT_EQ(MQTT_OK, err);
    ASSERT_NOT_NULL(list);

    ASSERT_EQ(sizeof(data), list->value.binary.len);
    ASSERT_MEM_EQ(data, list->value.binary.data, sizeof(data));

    mqtt_property_list_free(list);
}

/* ========================================================================== */
/* Test: Property Encoding                                                    */
/* ========================================================================== */

TEST(encode_empty_properties)
{
    mqtt_buffer_t buf;
    mqtt_buffer_init(&buf, 64);

    /* Empty property list should encode as just the length (0) */
    ssize_t len = mqtt_property_encode(&buf, NULL);
    ASSERT_TRUE(len >= 0);
    ASSERT_EQ(1, len);  /* Just the length field (0) */
    ASSERT_EQ(0, buf.data[0]);

    mqtt_buffer_cleanup(&buf);
}

TEST(encode_byte_property)
{
    mqtt_property_t *list = NULL;
    mqtt_buffer_t buf;
    mqtt_buffer_init(&buf, 64);

    mqtt_property_add_byte(&list, MQTT_PROP_PAYLOAD_FORMAT_INDICATOR, 1);

    ssize_t len = mqtt_property_encode(&buf, list);
    ASSERT_TRUE(len >= 0);

    /* Expected: length (2 bytes props = 0x02), prop_id (0x01), value (0x01) */
    ASSERT_EQ(3, len);
    ASSERT_EQ(0x02, buf.data[0]);  /* Property length */
    ASSERT_EQ(0x01, buf.data[1]);  /* MQTT_PROP_PAYLOAD_FORMAT_INDICATOR */
    ASSERT_EQ(0x01, buf.data[2]);  /* Value */

    mqtt_buffer_cleanup(&buf);
    mqtt_property_list_free(list);
}

TEST(encode_u16_property)
{
    mqtt_property_t *list = NULL;
    mqtt_buffer_t buf;
    mqtt_buffer_init(&buf, 64);

    mqtt_property_add_u16(&list, MQTT_PROP_SERVER_KEEP_ALIVE, 0x1234);

    ssize_t len = mqtt_property_encode(&buf, list);
    ASSERT_TRUE(len >= 0);

    /* Expected: length (3), prop_id (0x13), value_hi (0x12), value_lo (0x34) */
    ASSERT_EQ(4, len);
    ASSERT_EQ(0x03, buf.data[0]);
    ASSERT_EQ(0x13, buf.data[1]);  /* MQTT_PROP_SERVER_KEEP_ALIVE */
    ASSERT_EQ(0x12, buf.data[2]);
    ASSERT_EQ(0x34, buf.data[3]);

    mqtt_buffer_cleanup(&buf);
    mqtt_property_list_free(list);
}

TEST(encode_u32_property)
{
    mqtt_property_t *list = NULL;
    mqtt_buffer_t buf;
    mqtt_buffer_init(&buf, 64);

    mqtt_property_add_u32(&list, MQTT_PROP_SESSION_EXPIRY_INTERVAL, 0x12345678);

    ssize_t len = mqtt_property_encode(&buf, list);
    ASSERT_TRUE(len >= 0);

    /* Expected: length (5), prop_id (0x11), 4-byte value (big endian) */
    ASSERT_EQ(6, len);
    ASSERT_EQ(0x05, buf.data[0]);
    ASSERT_EQ(0x11, buf.data[1]);  /* MQTT_PROP_SESSION_EXPIRY_INTERVAL */
    ASSERT_EQ(0x12, buf.data[2]);
    ASSERT_EQ(0x34, buf.data[3]);
    ASSERT_EQ(0x56, buf.data[4]);
    ASSERT_EQ(0x78, buf.data[5]);

    mqtt_buffer_cleanup(&buf);
    mqtt_property_list_free(list);
}

TEST(encode_string_property)
{
    mqtt_property_t *list = NULL;
    mqtt_buffer_t buf;
    mqtt_buffer_init(&buf, 64);

    mqtt_property_add_string(&list, MQTT_PROP_CONTENT_TYPE, "test");

    ssize_t len = mqtt_property_encode(&buf, list);
    ASSERT_TRUE(len >= 0);

    /* Expected: length (7), prop_id (0x03), str_len (0x00 0x04), "test" */
    ASSERT_EQ(8, len);
    ASSERT_EQ(0x07, buf.data[0]);
    ASSERT_EQ(0x03, buf.data[1]);  /* MQTT_PROP_CONTENT_TYPE */
    ASSERT_EQ(0x00, buf.data[2]);  /* String length high byte */
    ASSERT_EQ(0x04, buf.data[3]);  /* String length low byte */
    ASSERT_EQ('t', buf.data[4]);
    ASSERT_EQ('e', buf.data[5]);
    ASSERT_EQ('s', buf.data[6]);
    ASSERT_EQ('t', buf.data[7]);

    mqtt_buffer_cleanup(&buf);
    mqtt_property_list_free(list);
}

/* ========================================================================== */
/* Test: Property Decoding                                                    */
/* ========================================================================== */

TEST(decode_empty_properties)
{
    uint8_t data[] = {0x00};  /* Property length = 0 */
    mqtt_property_t *list = NULL;
    size_t bytes_read = 0;

    mqtt_error_t err = mqtt_property_decode(data, sizeof(data), &list, &bytes_read);
    ASSERT_EQ(MQTT_OK, err);
    ASSERT_NULL(list);
    ASSERT_EQ(1, bytes_read);
}

TEST(decode_byte_property)
{
    /* Property length (2), prop_id (0x01), value (0x01) */
    uint8_t data[] = {0x02, 0x01, 0x01};
    mqtt_property_t *list = NULL;
    size_t bytes_read = 0;

    mqtt_error_t err = mqtt_property_decode(data, sizeof(data), &list, &bytes_read);
    ASSERT_EQ(MQTT_OK, err);
    ASSERT_NOT_NULL(list);
    ASSERT_EQ(3, bytes_read);
    ASSERT_EQ(MQTT_PROP_PAYLOAD_FORMAT_INDICATOR, list->id);
    ASSERT_EQ(1, list->value.byte);

    mqtt_property_list_free(list);
}

TEST(decode_u16_property)
{
    /* Property length (3), prop_id (0x13), value (0x12 0x34) */
    uint8_t data[] = {0x03, 0x13, 0x12, 0x34};
    mqtt_property_t *list = NULL;
    size_t bytes_read = 0;

    mqtt_error_t err = mqtt_property_decode(data, sizeof(data), &list, &bytes_read);
    ASSERT_EQ(MQTT_OK, err);
    ASSERT_NOT_NULL(list);
    ASSERT_EQ(4, bytes_read);
    ASSERT_EQ(MQTT_PROP_SERVER_KEEP_ALIVE, list->id);
    ASSERT_EQ(0x1234, list->value.u16);

    mqtt_property_list_free(list);
}

TEST(decode_string_property)
{
    /* Property length (7), prop_id (0x03), str_len (0x00 0x04), "test" */
    uint8_t data[] = {0x07, 0x03, 0x00, 0x04, 't', 'e', 's', 't'};
    mqtt_property_t *list = NULL;
    size_t bytes_read = 0;

    mqtt_error_t err = mqtt_property_decode(data, sizeof(data), &list, &bytes_read);
    ASSERT_EQ(MQTT_OK, err);
    ASSERT_NOT_NULL(list);
    ASSERT_EQ(8, bytes_read);
    ASSERT_EQ(MQTT_PROP_CONTENT_TYPE, list->id);
    ASSERT_STR_EQ("test", list->value.str);

    mqtt_property_list_free(list);
}

TEST(decode_multiple_properties)
{
    /* Two properties: byte and u16 */
    uint8_t data[] = {
        0x05,       /* Total property length */
        0x01, 0x01, /* PAYLOAD_FORMAT_INDICATOR = 1 */
        0x13, 0x00, 0x64  /* SERVER_KEEP_ALIVE = 100 */
    };
    mqtt_property_t *list = NULL;
    size_t bytes_read = 0;

    mqtt_error_t err = mqtt_property_decode(data, sizeof(data), &list, &bytes_read);
    ASSERT_EQ(MQTT_OK, err);
    ASSERT_NOT_NULL(list);
    ASSERT_EQ(6, bytes_read);
    ASSERT_EQ(2, mqtt_property_list_count(list));

    mqtt_property_list_free(list);
}

/* ========================================================================== */
/* Test: Roundtrip                                                            */
/* ========================================================================== */

TEST(roundtrip_properties)
{
    mqtt_property_t *original = NULL;
    mqtt_buffer_t buf;
    mqtt_buffer_init(&buf, 256);

    /* Create a mix of properties */
    mqtt_property_add_byte(&original, MQTT_PROP_PAYLOAD_FORMAT_INDICATOR, 1);
    mqtt_property_add_u16(&original, MQTT_PROP_RECEIVE_MAXIMUM, 100);
    mqtt_property_add_u32(&original, MQTT_PROP_SESSION_EXPIRY_INTERVAL, 3600);
    mqtt_property_add_string(&original, MQTT_PROP_CONTENT_TYPE, "application/json");
    mqtt_property_add_user_property(&original, "custom-key", "custom-value");

    /* Encode */
    ssize_t enc_len = mqtt_property_encode(&buf, original);
    ASSERT_TRUE(enc_len > 0);

    /* Decode */
    mqtt_property_t *decoded = NULL;
    size_t bytes_read = 0;
    mqtt_error_t err = mqtt_property_decode(buf.data, buf.len, &decoded, &bytes_read);
    ASSERT_EQ(MQTT_OK, err);
    ASSERT_EQ((size_t)enc_len, bytes_read);

    /* Verify count matches */
    ASSERT_EQ(mqtt_property_list_count(original), mqtt_property_list_count(decoded));

    /* Verify specific properties */
    mqtt_property_t *p = mqtt_property_list_find(decoded, MQTT_PROP_PAYLOAD_FORMAT_INDICATOR);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(1, p->value.byte);

    p = mqtt_property_list_find(decoded, MQTT_PROP_RECEIVE_MAXIMUM);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(100, p->value.u16);

    p = mqtt_property_list_find(decoded, MQTT_PROP_SESSION_EXPIRY_INTERVAL);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(3600, p->value.u32);

    p = mqtt_property_list_find(decoded, MQTT_PROP_CONTENT_TYPE);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ("application/json", p->value.str);

    mqtt_buffer_cleanup(&buf);
    mqtt_property_list_free(original);
    mqtt_property_list_free(decoded);
}

/* ========================================================================== */
/* Test: Property Validation                                                  */
/* ========================================================================== */

TEST(property_id_valid)
{
    ASSERT_TRUE(mqtt_property_id_valid(MQTT_PROP_PAYLOAD_FORMAT_INDICATOR));
    ASSERT_TRUE(mqtt_property_id_valid(MQTT_PROP_SESSION_EXPIRY_INTERVAL));
    ASSERT_TRUE(mqtt_property_id_valid(MQTT_PROP_USER_PROPERTY));
    ASSERT_FALSE(mqtt_property_id_valid(0x00));
    ASSERT_FALSE(mqtt_property_id_valid(0xFF));
}

TEST(property_name)
{
    const char *name = mqtt_property_name(MQTT_PROP_PAYLOAD_FORMAT_INDICATOR);
    ASSERT_NOT_NULL(name);
    ASSERT_STR_EQ("Payload Format Indicator", name);

    name = mqtt_property_name(0xFF);
    ASSERT_STR_EQ("Unknown", name);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void)
{
    TEST_SUITE_BEGIN("MQTT 5.0 Properties");

    /* Property creation */
    RUN_TEST(create_byte_property);
    RUN_TEST(create_u16_property);
    RUN_TEST(create_u32_property);
    RUN_TEST(create_string_property);
    RUN_TEST(create_binary_property);
    RUN_TEST(create_string_pair_property);

    /* List operations */
    RUN_TEST(list_append);
    RUN_TEST(list_find);
    RUN_TEST(add_user_property);
    RUN_TEST(add_binary_property);

    /* Encoding */
    RUN_TEST(encode_empty_properties);
    RUN_TEST(encode_byte_property);
    RUN_TEST(encode_u16_property);
    RUN_TEST(encode_u32_property);
    RUN_TEST(encode_string_property);

    /* Decoding */
    RUN_TEST(decode_empty_properties);
    RUN_TEST(decode_byte_property);
    RUN_TEST(decode_u16_property);
    RUN_TEST(decode_string_property);
    RUN_TEST(decode_multiple_properties);

    /* Roundtrip */
    RUN_TEST(roundtrip_properties);

    /* Validation */
    RUN_TEST(property_id_valid);
    RUN_TEST(property_name);

    TEST_SUITE_END();
}
