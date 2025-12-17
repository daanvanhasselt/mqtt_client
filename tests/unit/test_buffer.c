/**
 * @file test_buffer.c
 * @brief Unit tests for MQTT Buffer operations
 */

#include "test_framework.h"
#include "memory/mqtt_buffer.h"

static mqtt_buffer_t buf;

static void setup(void)
{
    mqtt_buffer_init(&buf, 64);
}

static void teardown(void)
{
    mqtt_buffer_cleanup(&buf);
}

/* ========================================================================== */
/* Test Cases                                                                 */
/* ========================================================================== */

TEST(init_creates_empty_buffer)
{
    mqtt_buffer_t b;
    mqtt_error_t err = mqtt_buffer_init(&b, 128);

    ASSERT_EQ(MQTT_OK, err);
    ASSERT_EQ(0, mqtt_buffer_len(&b));
    ASSERT_TRUE(mqtt_buffer_empty(&b));
    ASSERT_EQ(128, b.capacity);

    mqtt_buffer_cleanup(&b);
}

TEST(init_null_returns_error)
{
    mqtt_error_t err = mqtt_buffer_init(NULL, 64);
    ASSERT_EQ(MQTT_ERR_INVALID_ARG, err);
}

TEST(append_adds_data)
{
    setup();
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04};

    mqtt_error_t err = mqtt_buffer_append(&buf, data, sizeof(data));

    ASSERT_EQ(MQTT_OK, err);
    ASSERT_EQ(4, mqtt_buffer_len(&buf));
    ASSERT_FALSE(mqtt_buffer_empty(&buf));

    const uint8_t *ptr = mqtt_buffer_data_const(&buf);
    ASSERT_MEM_EQ(data, ptr, sizeof(data));

    teardown();
}

TEST(append_multiple)
{
    setup();
    const uint8_t data1[] = {0x01, 0x02};
    const uint8_t data2[] = {0x03, 0x04, 0x05};

    mqtt_buffer_append(&buf, data1, sizeof(data1));
    mqtt_buffer_append(&buf, data2, sizeof(data2));

    ASSERT_EQ(5, mqtt_buffer_len(&buf));

    const uint8_t expected[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    ASSERT_MEM_EQ(expected, mqtt_buffer_data_const(&buf), 5);

    teardown();
}

TEST(reset_clears_buffer)
{
    setup();
    const uint8_t data[] = {0x01, 0x02, 0x03};

    mqtt_buffer_append(&buf, data, sizeof(data));
    ASSERT_EQ(3, mqtt_buffer_len(&buf));

    mqtt_buffer_reset(&buf);

    ASSERT_EQ(0, mqtt_buffer_len(&buf));
    ASSERT_TRUE(mqtt_buffer_empty(&buf));

    teardown();
}

TEST(consume_removes_from_front)
{
    setup();
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};

    mqtt_buffer_append(&buf, data, sizeof(data));
    mqtt_buffer_consume(&buf, 2);

    ASSERT_EQ(3, mqtt_buffer_len(&buf));

    const uint8_t expected[] = {0x03, 0x04, 0x05};
    ASSERT_MEM_EQ(expected, mqtt_buffer_data_const(&buf), 3);

    teardown();
}

TEST(consume_all)
{
    setup();
    const uint8_t data[] = {0x01, 0x02, 0x03};

    mqtt_buffer_append(&buf, data, sizeof(data));
    mqtt_buffer_consume(&buf, 3);

    ASSERT_EQ(0, mqtt_buffer_len(&buf));
    ASSERT_TRUE(mqtt_buffer_empty(&buf));

    teardown();
}

TEST(auto_expand)
{
    mqtt_buffer_t b;
    mqtt_buffer_init(&b, 4);  /* Start small */

    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    mqtt_error_t err = mqtt_buffer_append(&b, data, sizeof(data));

    ASSERT_EQ(MQTT_OK, err);
    ASSERT_EQ(8, mqtt_buffer_len(&b));
    ASSERT_TRUE(b.capacity >= 8);
    ASSERT_MEM_EQ(data, mqtt_buffer_data_const(&b), 8);

    mqtt_buffer_cleanup(&b);
}

TEST(write_ptr_and_advance)
{
    setup();

    uint8_t *ptr = mqtt_buffer_write_ptr(&buf);
    ASSERT_NOT_NULL(ptr);

    /* Write directly */
    ptr[0] = 0xAA;
    ptr[1] = 0xBB;
    mqtt_buffer_advance_write(&buf, 2);

    ASSERT_EQ(2, mqtt_buffer_len(&buf));
    ASSERT_EQ(0xAA, mqtt_buffer_data_const(&buf)[0]);
    ASSERT_EQ(0xBB, mqtt_buffer_data_const(&buf)[1]);

    teardown();
}

TEST(write_available)
{
    setup();

    size_t avail = mqtt_buffer_write_available(&buf);
    ASSERT_EQ(64, avail);

    mqtt_buffer_append(&buf, (uint8_t *)"test", 4);
    avail = mqtt_buffer_write_available(&buf);
    ASSERT_EQ(60, avail);

    teardown();
}

TEST(reserve_increases_capacity)
{
    setup();

    mqtt_error_t err = mqtt_buffer_reserve(&buf, 256);
    ASSERT_EQ(MQTT_OK, err);
    ASSERT_TRUE(buf.capacity >= 256);

    teardown();
}

TEST(null_args_handled)
{
    mqtt_buffer_t b;

    ASSERT_EQ(MQTT_ERR_INVALID_ARG, mqtt_buffer_init(NULL, 64));
    ASSERT_EQ(MQTT_ERR_INVALID_ARG, mqtt_buffer_append(NULL, (uint8_t *)"x", 1));
    ASSERT_EQ(MQTT_ERR_INVALID_ARG, mqtt_buffer_reserve(NULL, 64));
    ASSERT_EQ(0, mqtt_buffer_len(NULL));
    ASSERT_TRUE(mqtt_buffer_empty(NULL));
    ASSERT_NULL(mqtt_buffer_data_const(NULL));
    ASSERT_NULL(mqtt_buffer_write_ptr(NULL));
    ASSERT_EQ(0, mqtt_buffer_write_available(NULL));

    /* These should not crash */
    mqtt_buffer_reset(NULL);
    mqtt_buffer_consume(NULL, 10);
    mqtt_buffer_advance_write(NULL, 10);
    mqtt_buffer_cleanup(NULL);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void)
{
    TEST_SUITE_BEGIN("Buffer");

    RUN_TEST(init_creates_empty_buffer);
    RUN_TEST(init_null_returns_error);
    RUN_TEST(append_adds_data);
    RUN_TEST(append_multiple);
    RUN_TEST(reset_clears_buffer);
    RUN_TEST(consume_removes_from_front);
    RUN_TEST(consume_all);
    RUN_TEST(auto_expand);
    RUN_TEST(write_ptr_and_advance);
    RUN_TEST(write_available);
    RUN_TEST(reserve_increases_capacity);
    RUN_TEST(null_args_handled);

    TEST_SUITE_END();
}
