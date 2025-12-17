/**
 * @file test_varint.c
 * @brief Unit tests for MQTT Variable Length Integer encoding/decoding
 */

#include "test_framework.h"
#include "core/mqtt_varint.h"

/* ========================================================================== */
/* Test Cases                                                                 */
/* ========================================================================== */

TEST(encode_single_byte_values)
{
    uint8_t buf[4];
    int len;

    /* 0 -> 0x00 */
    len = mqtt_varint_encode(0, buf);
    ASSERT_EQ(1, len);
    ASSERT_EQ(0x00, buf[0]);

    /* 127 -> 0x7F (max single byte) */
    len = mqtt_varint_encode(127, buf);
    ASSERT_EQ(1, len);
    ASSERT_EQ(0x7F, buf[0]);
}

TEST(encode_two_byte_values)
{
    uint8_t buf[4];
    int len;

    /* 128 -> 0x80 0x01 */
    len = mqtt_varint_encode(128, buf);
    ASSERT_EQ(2, len);
    ASSERT_EQ(0x80, buf[0]);
    ASSERT_EQ(0x01, buf[1]);

    /* 16383 -> 0xFF 0x7F (max two byte) */
    len = mqtt_varint_encode(16383, buf);
    ASSERT_EQ(2, len);
    ASSERT_EQ(0xFF, buf[0]);
    ASSERT_EQ(0x7F, buf[1]);
}

TEST(encode_three_byte_values)
{
    uint8_t buf[4];
    int len;

    /* 16384 -> 0x80 0x80 0x01 */
    len = mqtt_varint_encode(16384, buf);
    ASSERT_EQ(3, len);
    ASSERT_EQ(0x80, buf[0]);
    ASSERT_EQ(0x80, buf[1]);
    ASSERT_EQ(0x01, buf[2]);

    /* 2097151 -> 0xFF 0xFF 0x7F (max three byte) */
    len = mqtt_varint_encode(2097151, buf);
    ASSERT_EQ(3, len);
    ASSERT_EQ(0xFF, buf[0]);
    ASSERT_EQ(0xFF, buf[1]);
    ASSERT_EQ(0x7F, buf[2]);
}

TEST(encode_four_byte_values)
{
    uint8_t buf[4];
    int len;

    /* 2097152 -> 0x80 0x80 0x80 0x01 */
    len = mqtt_varint_encode(2097152, buf);
    ASSERT_EQ(4, len);
    ASSERT_EQ(0x80, buf[0]);
    ASSERT_EQ(0x80, buf[1]);
    ASSERT_EQ(0x80, buf[2]);
    ASSERT_EQ(0x01, buf[3]);

    /* 268435455 -> 0xFF 0xFF 0xFF 0x7F (max valid MQTT remaining length) */
    len = mqtt_varint_encode(268435455, buf);
    ASSERT_EQ(4, len);
    ASSERT_EQ(0xFF, buf[0]);
    ASSERT_EQ(0xFF, buf[1]);
    ASSERT_EQ(0xFF, buf[2]);
    ASSERT_EQ(0x7F, buf[3]);
}

TEST(decode_single_byte_values)
{
    uint32_t value;
    int len;

    uint8_t zero[] = {0x00};
    len = mqtt_varint_decode(zero, sizeof(zero), &value);
    ASSERT_EQ(1, len);
    ASSERT_EQ(0, value);

    uint8_t max_single[] = {0x7F};
    len = mqtt_varint_decode(max_single, sizeof(max_single), &value);
    ASSERT_EQ(1, len);
    ASSERT_EQ(127, value);
}

TEST(decode_two_byte_values)
{
    uint32_t value;
    int len;

    uint8_t min_two[] = {0x80, 0x01};
    len = mqtt_varint_decode(min_two, sizeof(min_two), &value);
    ASSERT_EQ(2, len);
    ASSERT_EQ(128, value);

    uint8_t max_two[] = {0xFF, 0x7F};
    len = mqtt_varint_decode(max_two, sizeof(max_two), &value);
    ASSERT_EQ(2, len);
    ASSERT_EQ(16383, value);
}

TEST(decode_three_byte_values)
{
    uint32_t value;
    int len;

    uint8_t min_three[] = {0x80, 0x80, 0x01};
    len = mqtt_varint_decode(min_three, sizeof(min_three), &value);
    ASSERT_EQ(3, len);
    ASSERT_EQ(16384, value);

    uint8_t max_three[] = {0xFF, 0xFF, 0x7F};
    len = mqtt_varint_decode(max_three, sizeof(max_three), &value);
    ASSERT_EQ(3, len);
    ASSERT_EQ(2097151, value);
}

TEST(decode_four_byte_values)
{
    uint32_t value;
    int len;

    uint8_t min_four[] = {0x80, 0x80, 0x80, 0x01};
    len = mqtt_varint_decode(min_four, sizeof(min_four), &value);
    ASSERT_EQ(4, len);
    ASSERT_EQ(2097152, value);

    uint8_t max_four[] = {0xFF, 0xFF, 0xFF, 0x7F};
    len = mqtt_varint_decode(max_four, sizeof(max_four), &value);
    ASSERT_EQ(4, len);
    ASSERT_EQ(268435455, value);
}

TEST(decode_incomplete)
{
    uint32_t value;
    int len;

    /* Continuation bit set but no next byte */
    uint8_t incomplete[] = {0x80};
    len = mqtt_varint_decode(incomplete, sizeof(incomplete), &value);
    ASSERT_EQ(-1, len);

    uint8_t incomplete2[] = {0x80, 0x80};
    len = mqtt_varint_decode(incomplete2, sizeof(incomplete2), &value);
    ASSERT_EQ(-1, len);
}

TEST(roundtrip)
{
    uint8_t buf[4];
    uint32_t decoded;

    /* Test various values roundtrip correctly */
    uint32_t test_values[] = {0, 1, 127, 128, 255, 16383, 16384, 2097151, 2097152, 268435455};

    for (size_t i = 0; i < sizeof(test_values) / sizeof(test_values[0]); i++) {
        int enc_len = mqtt_varint_encode(test_values[i], buf);
        ASSERT_TRUE(enc_len > 0);

        int dec_len = mqtt_varint_decode(buf, enc_len, &decoded);
        ASSERT_EQ(enc_len, dec_len);
        ASSERT_EQ(test_values[i], decoded);
    }
}

TEST(size_calculation)
{
    ASSERT_EQ(1, mqtt_varint_size(0));
    ASSERT_EQ(1, mqtt_varint_size(127));
    ASSERT_EQ(2, mqtt_varint_size(128));
    ASSERT_EQ(2, mqtt_varint_size(16383));
    ASSERT_EQ(3, mqtt_varint_size(16384));
    ASSERT_EQ(3, mqtt_varint_size(2097151));
    ASSERT_EQ(4, mqtt_varint_size(2097152));
    ASSERT_EQ(4, mqtt_varint_size(268435455));
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void)
{
    TEST_SUITE_BEGIN("Variable Length Integer");

    RUN_TEST(encode_single_byte_values);
    RUN_TEST(encode_two_byte_values);
    RUN_TEST(encode_three_byte_values);
    RUN_TEST(encode_four_byte_values);
    RUN_TEST(decode_single_byte_values);
    RUN_TEST(decode_two_byte_values);
    RUN_TEST(decode_three_byte_values);
    RUN_TEST(decode_four_byte_values);
    RUN_TEST(decode_incomplete);
    RUN_TEST(roundtrip);
    RUN_TEST(size_calculation);

    TEST_SUITE_END();
}
