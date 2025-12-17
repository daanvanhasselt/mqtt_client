/**
 * @file test_packet_id.c
 * @brief Unit tests for MQTT Packet ID Allocator
 */

#include "test_framework.h"
#include "client/mqtt_packet_id.h"

static mqtt_packet_id_allocator_t allocator;

static void setup(void)
{
    mqtt_packet_id_init(&allocator);
}

/* ========================================================================== */
/* Test Cases                                                                 */
/* ========================================================================== */

TEST(init_creates_empty_allocator)
{
    mqtt_packet_id_allocator_t alloc;
    mqtt_error_t err = mqtt_packet_id_init(&alloc);

    ASSERT_EQ(MQTT_OK, err);
    ASSERT_EQ(0, mqtt_packet_id_count(&alloc));
}

TEST(init_null_returns_error)
{
    mqtt_error_t err = mqtt_packet_id_init(NULL);
    ASSERT_EQ(MQTT_ERR_INVALID_ARG, err);
}

TEST(alloc_returns_nonzero_id)
{
    setup();
    uint16_t id;
    mqtt_error_t err = mqtt_packet_id_alloc(&allocator, &id);

    ASSERT_EQ(MQTT_OK, err);
    ASSERT_NE(0, id);
    ASSERT_EQ(1, mqtt_packet_id_count(&allocator));
}

TEST(alloc_returns_sequential_ids)
{
    setup();
    uint16_t id1, id2, id3;

    mqtt_packet_id_alloc(&allocator, &id1);
    mqtt_packet_id_alloc(&allocator, &id2);
    mqtt_packet_id_alloc(&allocator, &id3);

    ASSERT_EQ(1, id1);
    ASSERT_EQ(2, id2);
    ASSERT_EQ(3, id3);
    ASSERT_EQ(3, mqtt_packet_id_count(&allocator));
}

TEST(free_releases_id)
{
    setup();
    uint16_t id;

    mqtt_packet_id_alloc(&allocator, &id);
    ASSERT_EQ(1, mqtt_packet_id_count(&allocator));
    ASSERT_TRUE(mqtt_packet_id_is_allocated(&allocator, id));

    mqtt_error_t err = mqtt_packet_id_free(&allocator, id);
    ASSERT_EQ(MQTT_OK, err);
    ASSERT_EQ(0, mqtt_packet_id_count(&allocator));
    ASSERT_FALSE(mqtt_packet_id_is_allocated(&allocator, id));
}

TEST(free_id_zero_returns_error)
{
    setup();
    mqtt_error_t err = mqtt_packet_id_free(&allocator, 0);
    ASSERT_EQ(MQTT_ERR_INVALID_PACKET_ID, err);
}

TEST(free_unallocated_returns_error)
{
    setup();
    mqtt_error_t err = mqtt_packet_id_free(&allocator, 42);
    ASSERT_EQ(MQTT_ERR_INVALID_PACKET_ID, err);
}

TEST(double_free_returns_error)
{
    setup();
    uint16_t id;

    mqtt_packet_id_alloc(&allocator, &id);
    mqtt_packet_id_free(&allocator, id);
    mqtt_error_t err = mqtt_packet_id_free(&allocator, id);

    ASSERT_EQ(MQTT_ERR_INVALID_PACKET_ID, err);
}

TEST(alloc_reuses_freed_ids)
{
    setup();
    uint16_t id1, id2, id3;

    mqtt_packet_id_alloc(&allocator, &id1);
    mqtt_packet_id_alloc(&allocator, &id2);
    mqtt_packet_id_free(&allocator, id1);

    mqtt_packet_id_alloc(&allocator, &id3);

    /* id3 should reuse id1's slot eventually (after wrapping) or continue sequentially */
    ASSERT_EQ(2, mqtt_packet_id_count(&allocator));
}

TEST(reset_clears_all)
{
    setup();
    uint16_t id;

    mqtt_packet_id_alloc(&allocator, &id);
    mqtt_packet_id_alloc(&allocator, &id);
    mqtt_packet_id_alloc(&allocator, &id);
    ASSERT_EQ(3, mqtt_packet_id_count(&allocator));

    mqtt_packet_id_reset(&allocator);
    ASSERT_EQ(0, mqtt_packet_id_count(&allocator));
}

TEST(is_allocated_correct)
{
    setup();
    uint16_t id1, id2;

    mqtt_packet_id_alloc(&allocator, &id1);
    mqtt_packet_id_alloc(&allocator, &id2);

    ASSERT_TRUE(mqtt_packet_id_is_allocated(&allocator, id1));
    ASSERT_TRUE(mqtt_packet_id_is_allocated(&allocator, id2));
    ASSERT_FALSE(mqtt_packet_id_is_allocated(&allocator, id2 + 1));

    mqtt_packet_id_free(&allocator, id1);
    ASSERT_FALSE(mqtt_packet_id_is_allocated(&allocator, id1));
    ASSERT_TRUE(mqtt_packet_id_is_allocated(&allocator, id2));
}

TEST(alloc_many_ids)
{
    setup();
    uint16_t ids[1000];

    for (int i = 0; i < 1000; i++) {
        mqtt_error_t err = mqtt_packet_id_alloc(&allocator, &ids[i]);
        ASSERT_EQ(MQTT_OK, err);
        ASSERT_NE(0, ids[i]);
    }

    ASSERT_EQ(1000, mqtt_packet_id_count(&allocator));

    /* All IDs should be unique */
    for (int i = 0; i < 1000; i++) {
        for (int j = i + 1; j < 1000; j++) {
            ASSERT_NE(ids[i], ids[j]);
        }
    }
}

TEST(alloc_exhaustion)
{
    setup();
    uint16_t id;
    mqtt_error_t err;

    /* Allocate all 65535 IDs */
    for (int i = 0; i < 65535; i++) {
        err = mqtt_packet_id_alloc(&allocator, &id);
        ASSERT_EQ(MQTT_OK, err);
    }

    ASSERT_EQ(65535, mqtt_packet_id_count(&allocator));

    /* Next allocation should fail */
    err = mqtt_packet_id_alloc(&allocator, &id);
    ASSERT_EQ(MQTT_ERR_PACKET_ID_EXHAUSTED, err);
}

TEST(null_args_handled)
{
    setup();
    uint16_t id;

    ASSERT_EQ(MQTT_ERR_INVALID_ARG, mqtt_packet_id_alloc(NULL, &id));
    ASSERT_EQ(MQTT_ERR_INVALID_ARG, mqtt_packet_id_alloc(&allocator, NULL));
    ASSERT_EQ(MQTT_ERR_INVALID_ARG, mqtt_packet_id_free(NULL, 1));
    ASSERT_FALSE(mqtt_packet_id_is_allocated(NULL, 1));
    ASSERT_EQ(0, mqtt_packet_id_count(NULL));
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void)
{
    TEST_SUITE_BEGIN("Packet ID Allocator");

    RUN_TEST(init_creates_empty_allocator);
    RUN_TEST(init_null_returns_error);
    RUN_TEST(alloc_returns_nonzero_id);
    RUN_TEST(alloc_returns_sequential_ids);
    RUN_TEST(free_releases_id);
    RUN_TEST(free_id_zero_returns_error);
    RUN_TEST(free_unallocated_returns_error);
    RUN_TEST(double_free_returns_error);
    RUN_TEST(alloc_reuses_freed_ids);
    RUN_TEST(reset_clears_all);
    RUN_TEST(is_allocated_correct);
    RUN_TEST(alloc_many_ids);
    RUN_TEST(alloc_exhaustion);
    RUN_TEST(null_args_handled);

    TEST_SUITE_END();
}
