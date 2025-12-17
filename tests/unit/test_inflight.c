/**
 * @file test_inflight.c
 * @brief Unit tests for MQTT Inflight Message Queue
 */

#include "test_framework.h"
#include "client/mqtt_inflight.h"

static mqtt_inflight_queue_t queue;

static void setup(void)
{
    mqtt_inflight_init(&queue, 10);  /* Max 10 inflight */
}

static void teardown(void)
{
    mqtt_inflight_cleanup(&queue);
}

/* ========================================================================== */
/* Test Cases                                                                 */
/* ========================================================================== */

TEST(init_creates_empty_queue)
{
    mqtt_inflight_queue_t q;
    mqtt_error_t err = mqtt_inflight_init(&q, 20);

    ASSERT_EQ(MQTT_OK, err);
    ASSERT_EQ(0, mqtt_inflight_count(&q));
    ASSERT_FALSE(mqtt_inflight_is_full(&q));
    ASSERT_EQ(20, q.max_count);

    mqtt_inflight_cleanup(&q);
}

TEST(init_null_returns_error)
{
    mqtt_error_t err = mqtt_inflight_init(NULL, 10);
    ASSERT_EQ(MQTT_ERR_INVALID_ARG, err);
}

TEST(add_qos1_message)
{
    setup();
    const char *topic = "test/topic";
    const uint8_t payload[] = "hello";

    mqtt_error_t err = mqtt_inflight_add(&queue, 1, MQTT_QOS_1,
                                          topic, payload, sizeof(payload),
                                          false, 1000);

    ASSERT_EQ(MQTT_OK, err);
    ASSERT_EQ(1, mqtt_inflight_count(&queue));

    mqtt_inflight_entry_t *entry = mqtt_inflight_find(&queue, 1);
    ASSERT_NOT_NULL(entry);
    ASSERT_EQ(1, entry->packet_id);
    ASSERT_EQ(MQTT_QOS_1, entry->qos);
    ASSERT_EQ(MQTT_INFLIGHT_PENDING, entry->state);
    ASSERT_STR_EQ(topic, entry->topic);
    ASSERT_MEM_EQ(payload, entry->payload, sizeof(payload));
    ASSERT_EQ(sizeof(payload), entry->payload_len);
    ASSERT_FALSE(entry->retain);
    ASSERT_EQ(1000, entry->send_time);
    ASSERT_EQ(0, entry->retry_count);

    teardown();
}

TEST(add_qos2_message)
{
    setup();

    mqtt_error_t err = mqtt_inflight_add(&queue, 42, MQTT_QOS_2,
                                          "sensor/temp", NULL, 0,
                                          true, 5000);

    ASSERT_EQ(MQTT_OK, err);

    mqtt_inflight_entry_t *entry = mqtt_inflight_find(&queue, 42);
    ASSERT_NOT_NULL(entry);
    ASSERT_EQ(MQTT_QOS_2, entry->qos);
    ASSERT_TRUE(entry->retain);
    ASSERT_NULL(entry->payload);
    ASSERT_EQ(0, entry->payload_len);

    teardown();
}

TEST(add_qos0_rejected)
{
    setup();

    mqtt_error_t err = mqtt_inflight_add(&queue, 1, MQTT_QOS_0,
                                          "test", NULL, 0, false, 0);

    ASSERT_EQ(MQTT_ERR_INVALID_ARG, err);
    ASSERT_EQ(0, mqtt_inflight_count(&queue));

    teardown();
}

TEST(add_multiple_messages)
{
    setup();

    for (uint16_t i = 1; i <= 5; i++) {
        char topic[32];
        snprintf(topic, sizeof(topic), "topic/%d", i);
        mqtt_error_t err = mqtt_inflight_add(&queue, i, MQTT_QOS_1,
                                              topic, NULL, 0, false, i * 1000);
        ASSERT_EQ(MQTT_OK, err);
    }

    ASSERT_EQ(5, mqtt_inflight_count(&queue));

    /* Verify all can be found */
    for (uint16_t i = 1; i <= 5; i++) {
        mqtt_inflight_entry_t *entry = mqtt_inflight_find(&queue, i);
        ASSERT_NOT_NULL(entry);
        ASSERT_EQ(i, entry->packet_id);
    }

    teardown();
}

TEST(queue_full_rejects)
{
    setup();  /* max 10 */

    /* Fill the queue */
    for (uint16_t i = 1; i <= 10; i++) {
        mqtt_error_t err = mqtt_inflight_add(&queue, i, MQTT_QOS_1,
                                              "test", NULL, 0, false, 0);
        ASSERT_EQ(MQTT_OK, err);
    }

    ASSERT_TRUE(mqtt_inflight_is_full(&queue));

    /* 11th should fail */
    mqtt_error_t err = mqtt_inflight_add(&queue, 11, MQTT_QOS_1,
                                          "test", NULL, 0, false, 0);
    ASSERT_EQ(MQTT_ERR_INFLIGHT_FULL, err);
    ASSERT_EQ(10, mqtt_inflight_count(&queue));

    teardown();
}

TEST(unlimited_queue)
{
    mqtt_inflight_queue_t unlimited;
    mqtt_inflight_init(&unlimited, 0);  /* 0 = unlimited */

    ASSERT_FALSE(mqtt_inflight_is_full(&unlimited));

    /* Add many messages */
    for (uint16_t i = 1; i <= 100; i++) {
        mqtt_error_t err = mqtt_inflight_add(&unlimited, i, MQTT_QOS_1,
                                              "test", NULL, 0, false, 0);
        ASSERT_EQ(MQTT_OK, err);
    }

    ASSERT_EQ(100, mqtt_inflight_count(&unlimited));
    ASSERT_FALSE(mqtt_inflight_is_full(&unlimited));

    mqtt_inflight_cleanup(&unlimited);
}

TEST(find_nonexistent_returns_null)
{
    setup();

    mqtt_inflight_add(&queue, 1, MQTT_QOS_1, "test", NULL, 0, false, 0);

    ASSERT_NULL(mqtt_inflight_find(&queue, 2));
    ASSERT_NULL(mqtt_inflight_find(&queue, 999));

    teardown();
}

TEST(remove_entry)
{
    setup();

    mqtt_inflight_add(&queue, 1, MQTT_QOS_1, "test1", NULL, 0, false, 0);
    mqtt_inflight_add(&queue, 2, MQTT_QOS_1, "test2", NULL, 0, false, 0);
    mqtt_inflight_add(&queue, 3, MQTT_QOS_1, "test3", NULL, 0, false, 0);

    mqtt_inflight_entry_t *entry = mqtt_inflight_find(&queue, 2);
    ASSERT_NOT_NULL(entry);

    mqtt_inflight_remove(&queue, entry);

    ASSERT_EQ(2, mqtt_inflight_count(&queue));
    ASSERT_NULL(mqtt_inflight_find(&queue, 2));
    ASSERT_NOT_NULL(mqtt_inflight_find(&queue, 1));
    ASSERT_NOT_NULL(mqtt_inflight_find(&queue, 3));

    teardown();
}

TEST(remove_head)
{
    setup();

    mqtt_inflight_add(&queue, 1, MQTT_QOS_1, "test1", NULL, 0, false, 0);
    mqtt_inflight_add(&queue, 2, MQTT_QOS_1, "test2", NULL, 0, false, 0);

    mqtt_inflight_entry_t *entry = mqtt_inflight_find(&queue, 1);
    mqtt_inflight_remove(&queue, entry);

    ASSERT_EQ(1, mqtt_inflight_count(&queue));
    ASSERT_NULL(mqtt_inflight_find(&queue, 1));
    ASSERT_NOT_NULL(mqtt_inflight_find(&queue, 2));

    teardown();
}

TEST(remove_tail)
{
    setup();

    mqtt_inflight_add(&queue, 1, MQTT_QOS_1, "test1", NULL, 0, false, 0);
    mqtt_inflight_add(&queue, 2, MQTT_QOS_1, "test2", NULL, 0, false, 0);

    mqtt_inflight_entry_t *entry = mqtt_inflight_find(&queue, 2);
    mqtt_inflight_remove(&queue, entry);

    ASSERT_EQ(1, mqtt_inflight_count(&queue));
    ASSERT_NOT_NULL(mqtt_inflight_find(&queue, 1));
    ASSERT_NULL(mqtt_inflight_find(&queue, 2));

    teardown();
}

TEST(clear_removes_all)
{
    setup();

    mqtt_inflight_add(&queue, 1, MQTT_QOS_1, "test1", NULL, 0, false, 0);
    mqtt_inflight_add(&queue, 2, MQTT_QOS_1, "test2", NULL, 0, false, 0);
    mqtt_inflight_add(&queue, 3, MQTT_QOS_1, "test3", NULL, 0, false, 0);

    mqtt_inflight_clear(&queue);

    ASSERT_EQ(0, mqtt_inflight_count(&queue));
    ASSERT_NULL(queue.head);
    ASSERT_NULL(queue.tail);

    teardown();
}

TEST(update_state)
{
    setup();

    mqtt_inflight_add(&queue, 1, MQTT_QOS_2, "test", NULL, 0, false, 1000);

    mqtt_inflight_entry_t *entry = mqtt_inflight_find(&queue, 1);
    ASSERT_EQ(MQTT_INFLIGHT_PENDING, entry->state);

    mqtt_inflight_update_state(entry, MQTT_INFLIGHT_PUBREC, 2000);
    ASSERT_EQ(MQTT_INFLIGHT_PUBREC, entry->state);
    ASSERT_EQ(2000, entry->send_time);
    ASSERT_EQ(0, entry->retry_count);  /* Reset on state change */

    mqtt_inflight_update_state(entry, MQTT_INFLIGHT_PUBREL, 3000);
    ASSERT_EQ(MQTT_INFLIGHT_PUBREL, entry->state);

    teardown();
}

TEST(retry_detection)
{
    setup();
    mqtt_inflight_set_retry_config(&queue, 1000, 3);  /* 1 second timeout */

    mqtt_inflight_add(&queue, 1, MQTT_QOS_1, "test1", NULL, 0, false, 0);
    mqtt_inflight_add(&queue, 2, MQTT_QOS_1, "test2", NULL, 0, false, 500);

    /* At time 500, nothing needs retry */
    ASSERT_NULL(mqtt_inflight_get_retry(&queue, 500));

    /* At time 1000, entry 1 needs retry (sent at 0, timeout at 1000) */
    mqtt_inflight_entry_t *entry = mqtt_inflight_get_retry(&queue, 1000);
    ASSERT_NOT_NULL(entry);
    ASSERT_EQ(1, entry->packet_id);

    /* Mark as retried */
    mqtt_inflight_mark_retried(entry, 1000);
    ASSERT_EQ(1, entry->retry_count);
    ASSERT_EQ(1000, entry->send_time);

    /* At time 1500, entry 2 needs retry (sent at 500, timeout at 1500) */
    entry = mqtt_inflight_get_retry(&queue, 1500);
    ASSERT_NOT_NULL(entry);
    ASSERT_EQ(2, entry->packet_id);

    teardown();
}

TEST(max_retries_stops_retry)
{
    setup();
    mqtt_inflight_set_retry_config(&queue, 100, 2);  /* 2 max retries */

    mqtt_inflight_add(&queue, 1, MQTT_QOS_1, "test", NULL, 0, false, 0);

    mqtt_inflight_entry_t *entry;

    /* First retry at time 100 */
    entry = mqtt_inflight_get_retry(&queue, 100);
    ASSERT_NOT_NULL(entry);
    mqtt_inflight_mark_retried(entry, 100);

    /* Second retry at time 200 */
    entry = mqtt_inflight_get_retry(&queue, 200);
    ASSERT_NOT_NULL(entry);
    mqtt_inflight_mark_retried(entry, 200);

    /* Third attempt - should NOT be returned (max retries reached) */
    entry = mqtt_inflight_get_retry(&queue, 300);
    ASSERT_NULL(entry);

    teardown();
}

TEST(null_args_handled)
{
    ASSERT_EQ(MQTT_ERR_INVALID_ARG, mqtt_inflight_add(NULL, 1, MQTT_QOS_1, "t", NULL, 0, false, 0));
    ASSERT_EQ(MQTT_ERR_INVALID_ARG, mqtt_inflight_add(&queue, 1, MQTT_QOS_1, NULL, NULL, 0, false, 0));
    ASSERT_NULL(mqtt_inflight_find(NULL, 1));
    ASSERT_TRUE(mqtt_inflight_is_full(NULL));
    ASSERT_EQ(0, mqtt_inflight_count(NULL));
    ASSERT_NULL(mqtt_inflight_get_retry(NULL, 0));

    /* These should not crash */
    mqtt_inflight_remove(NULL, NULL);
    mqtt_inflight_update_state(NULL, MQTT_INFLIGHT_PUBREL, 0);
    mqtt_inflight_mark_retried(NULL, 0);
    mqtt_inflight_cleanup(NULL);
    mqtt_inflight_clear(NULL);
    mqtt_inflight_set_retry_config(NULL, 0, 0);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void)
{
    TEST_SUITE_BEGIN("Inflight Queue");

    RUN_TEST(init_creates_empty_queue);
    RUN_TEST(init_null_returns_error);
    RUN_TEST(add_qos1_message);
    RUN_TEST(add_qos2_message);
    RUN_TEST(add_qos0_rejected);
    RUN_TEST(add_multiple_messages);
    RUN_TEST(queue_full_rejects);
    RUN_TEST(unlimited_queue);
    RUN_TEST(find_nonexistent_returns_null);
    RUN_TEST(remove_entry);
    RUN_TEST(remove_head);
    RUN_TEST(remove_tail);
    RUN_TEST(clear_removes_all);
    RUN_TEST(update_state);
    RUN_TEST(retry_detection);
    RUN_TEST(max_retries_stops_retry);
    RUN_TEST(null_args_handled);

    TEST_SUITE_END();
}
