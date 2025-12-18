/**
 * @file test_broker_connect.c
 * @brief Integration tests with real MQTT brokers
 *
 * Tests basic connectivity and operations with public MQTT test brokers.
 * These tests require network access and may be skipped in CI environments.
 *
 * Public test brokers used:
 * - test.mosquitto.org (ports 1883, 8883)
 * - broker.hivemq.com (port 1883)
 */

#define _DEFAULT_SOURCE  /* For usleep */

#include "../test_framework.h"
#include <mqtt/mqtt.h>
#include <mqtt/mqtt_types.h>
#include <mqtt/mqtt_error.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>

/* Test broker configurations */
#define MOSQUITTO_HOST "test.mosquitto.org"
#define MOSQUITTO_PORT 1883

#define HIVEMQ_HOST    "broker.hivemq.com"
#define HIVEMQ_PORT    1883

/* Test timeouts */
#define CONNECT_TIMEOUT_MS  10000
#define OPERATION_TIMEOUT_MS 5000
#define POLL_INTERVAL_MS    100

/* Test state */
static mqtt_client_t *g_client = NULL;
static bool g_connected = false;
static bool g_message_received = false;
static bool g_subscribed = false;
static bool g_published = false;
static char g_received_topic[256] = {0};
static char g_received_payload[1024] = {0};
static size_t g_received_payload_len = 0;

/*******************************************************************************
 * Callbacks - match signatures in mqtt_types.h
 ******************************************************************************/

static void on_connect(mqtt_client_t *client, void *user_data, bool session_present)
{
    (void)client;
    (void)user_data;
    (void)session_present;

    g_connected = true;
    printf("(connected) ");
    fflush(stdout);
}

static void on_disconnect(mqtt_client_t *client, void *user_data, int reason)
{
    (void)client;
    (void)user_data;
    (void)reason;
    g_connected = false;
}

static void on_subscribe(mqtt_client_t *client, void *user_data,
                         uint16_t packet_id, const mqtt_qos_t *granted_qos,
                         size_t count)
{
    (void)client;
    (void)user_data;
    (void)packet_id;
    (void)granted_qos;
    (void)count;
    g_subscribed = true;
}

static void on_publish_complete(mqtt_client_t *client, void *user_data,
                                uint16_t packet_id)
{
    (void)client;
    (void)user_data;
    (void)packet_id;
    g_published = true;
}

static void on_message(mqtt_client_t *client, void *user_data,
                       const mqtt_message_t *message)
{
    (void)client;
    (void)user_data;

    g_message_received = true;

    if (message && message->topic) {
        strncpy(g_received_topic, message->topic, sizeof(g_received_topic) - 1);
    }

    if (message && message->payload && message->payload_len > 0) {
        size_t copy_len = message->payload_len;
        if (copy_len > sizeof(g_received_payload) - 1) {
            copy_len = sizeof(g_received_payload) - 1;
        }
        memcpy(g_received_payload, message->payload, copy_len);
        g_received_payload[copy_len] = '\0';
        g_received_payload_len = copy_len;
    }
}

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

static void reset_state(void)
{
    g_connected = false;
    g_message_received = false;
    g_subscribed = false;
    g_published = false;
    memset(g_received_topic, 0, sizeof(g_received_topic));
    memset(g_received_payload, 0, sizeof(g_received_payload));
    g_received_payload_len = 0;
}

static bool wait_for_condition(bool *condition, int timeout_ms)
{
    int elapsed = 0;

    while (!*condition && elapsed < timeout_ms) {
        if (g_client) {
            mqtt_loop(g_client, POLL_INTERVAL_MS);
        } else {
            usleep(POLL_INTERVAL_MS * 1000);
        }
        elapsed += POLL_INTERVAL_MS;
    }

    return *condition;
}

static char *generate_client_id(char *buf, size_t len)
{
    snprintf(buf, len, "mqtt_test_%ld_%d", (long)time(NULL), getpid() % 10000);
    return buf;
}

static char *generate_topic(char *buf, size_t len)
{
    snprintf(buf, len, "mqtt_client_test/%ld/%d", (long)time(NULL), getpid() % 10000);
    return buf;
}

/*******************************************************************************
 * Setup / Teardown
 ******************************************************************************/

static void test_setup(void)
{
    reset_state();
    g_client = NULL;
}

static void test_teardown(void)
{
    if (g_client) {
        mqtt_disconnect(g_client);
        mqtt_loop(g_client, 500);  /* Give time for DISCONNECT */
        mqtt_client_destroy(g_client);
        g_client = NULL;
    }
    reset_state();
}

/*******************************************************************************
 * Tests - Basic Connectivity
 ******************************************************************************/

TEST(connect_mosquitto)
{
    char client_id[64];
    generate_client_id(client_id, sizeof(client_id));

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.transport_type = MQTT_TRANSPORT_TCP;

    g_client = mqtt_client_create(&config);
    ASSERT_NOT_NULL(g_client);

    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_disconnect = on_disconnect;
    mqtt_set_callbacks(g_client, &callbacks);

    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = MOSQUITTO_HOST;
    conn_opts.port = MOSQUITTO_PORT;
    conn_opts.client_id = client_id;
    conn_opts.keepalive_sec = 60;
    conn_opts.clean_session = true;

    mqtt_error_t err = mqtt_connect_async(g_client, &conn_opts);
    ASSERT_EQ(MQTT_OK, err);

    bool connected = wait_for_condition(&g_connected, CONNECT_TIMEOUT_MS);
    ASSERT_TRUE(connected);
}

TEST(connect_hivemq)
{
    char client_id[64];
    generate_client_id(client_id, sizeof(client_id));

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.transport_type = MQTT_TRANSPORT_TCP;

    g_client = mqtt_client_create(&config);
    ASSERT_NOT_NULL(g_client);

    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_disconnect = on_disconnect;
    mqtt_set_callbacks(g_client, &callbacks);

    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = HIVEMQ_HOST;
    conn_opts.port = HIVEMQ_PORT;
    conn_opts.client_id = client_id;
    conn_opts.keepalive_sec = 60;
    conn_opts.clean_session = true;

    mqtt_error_t err = mqtt_connect_async(g_client, &conn_opts);
    ASSERT_EQ(MQTT_OK, err);

    bool connected = wait_for_condition(&g_connected, CONNECT_TIMEOUT_MS);
    ASSERT_TRUE(connected);
}

/*******************************************************************************
 * Tests - Publish/Subscribe
 ******************************************************************************/

TEST(pubsub_qos0)
{
    char client_id[64];
    char topic[128];
    generate_client_id(client_id, sizeof(client_id));
    generate_topic(topic, sizeof(topic));

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.transport_type = MQTT_TRANSPORT_TCP;

    g_client = mqtt_client_create(&config);
    ASSERT_NOT_NULL(g_client);

    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_disconnect = on_disconnect;
    callbacks.on_subscribe = on_subscribe;
    callbacks.on_message = on_message;
    mqtt_set_callbacks(g_client, &callbacks);

    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = MOSQUITTO_HOST;
    conn_opts.port = MOSQUITTO_PORT;
    conn_opts.client_id = client_id;
    conn_opts.keepalive_sec = 60;
    conn_opts.clean_session = true;

    mqtt_error_t err = mqtt_connect_async(g_client, &conn_opts);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_connected, CONNECT_TIMEOUT_MS));

    /* Subscribe */
    mqtt_subscribe_opts_t sub_opts = {0};
    sub_opts.topic_filter = topic;
    sub_opts.max_qos = MQTT_QOS_0;

    err = mqtt_subscribe_async(g_client, &sub_opts, 1, NULL);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_subscribed, OPERATION_TIMEOUT_MS));

    /* Publish */
    const char *payload = "Hello QoS 0";
    mqtt_publish_opts_t pub_opts = {0};
    pub_opts.topic = topic;
    pub_opts.payload = (const uint8_t *)payload;
    pub_opts.payload_len = strlen(payload);
    pub_opts.qos = MQTT_QOS_0;
    pub_opts.retain = false;

    err = mqtt_publish_async(g_client, &pub_opts, NULL);
    ASSERT_EQ(MQTT_OK, err);

    /* Wait for message (may not arrive with QoS 0, but give it a chance) */
    wait_for_condition(&g_message_received, OPERATION_TIMEOUT_MS);

    /* QoS 0 doesn't guarantee delivery, so we just check we didn't crash */
}

TEST(pubsub_qos1)
{
    char client_id[64];
    char topic[128];
    generate_client_id(client_id, sizeof(client_id));
    generate_topic(topic, sizeof(topic));

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.transport_type = MQTT_TRANSPORT_TCP;

    g_client = mqtt_client_create(&config);
    ASSERT_NOT_NULL(g_client);

    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_disconnect = on_disconnect;
    callbacks.on_subscribe = on_subscribe;
    callbacks.on_publish_complete = on_publish_complete;
    callbacks.on_message = on_message;
    mqtt_set_callbacks(g_client, &callbacks);

    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = MOSQUITTO_HOST;
    conn_opts.port = MOSQUITTO_PORT;
    conn_opts.client_id = client_id;
    conn_opts.keepalive_sec = 60;
    conn_opts.clean_session = true;

    mqtt_error_t err = mqtt_connect_async(g_client, &conn_opts);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_connected, CONNECT_TIMEOUT_MS));

    /* Subscribe */
    mqtt_subscribe_opts_t sub_opts = {0};
    sub_opts.topic_filter = topic;
    sub_opts.max_qos = MQTT_QOS_1;

    err = mqtt_subscribe_async(g_client, &sub_opts, 1, NULL);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_subscribed, OPERATION_TIMEOUT_MS));

    /* Publish */
    const char *payload = "Hello QoS 1";
    mqtt_publish_opts_t pub_opts = {0};
    pub_opts.topic = topic;
    pub_opts.payload = (const uint8_t *)payload;
    pub_opts.payload_len = strlen(payload);
    pub_opts.qos = MQTT_QOS_1;
    pub_opts.retain = false;

    err = mqtt_publish_async(g_client, &pub_opts, NULL);
    ASSERT_EQ(MQTT_OK, err);

    /* Wait for PUBACK */
    ASSERT_TRUE(wait_for_condition(&g_published, OPERATION_TIMEOUT_MS));

    /* Wait for message */
    ASSERT_TRUE(wait_for_condition(&g_message_received, OPERATION_TIMEOUT_MS));

    /* Verify message content */
    ASSERT_STR_EQ(topic, g_received_topic);
    ASSERT_STR_EQ(payload, g_received_payload);
}

TEST(pubsub_qos2)
{
    char client_id[64];
    char topic[128];
    generate_client_id(client_id, sizeof(client_id));
    generate_topic(topic, sizeof(topic));

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.transport_type = MQTT_TRANSPORT_TCP;

    g_client = mqtt_client_create(&config);
    ASSERT_NOT_NULL(g_client);

    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_disconnect = on_disconnect;
    callbacks.on_subscribe = on_subscribe;
    callbacks.on_publish_complete = on_publish_complete;
    callbacks.on_message = on_message;
    mqtt_set_callbacks(g_client, &callbacks);

    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = MOSQUITTO_HOST;
    conn_opts.port = MOSQUITTO_PORT;
    conn_opts.client_id = client_id;
    conn_opts.keepalive_sec = 60;
    conn_opts.clean_session = true;

    mqtt_error_t err = mqtt_connect_async(g_client, &conn_opts);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_connected, CONNECT_TIMEOUT_MS));

    /* Subscribe */
    mqtt_subscribe_opts_t sub_opts = {0};
    sub_opts.topic_filter = topic;
    sub_opts.max_qos = MQTT_QOS_2;

    err = mqtt_subscribe_async(g_client, &sub_opts, 1, NULL);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_subscribed, OPERATION_TIMEOUT_MS));

    /* Publish */
    const char *payload = "Hello QoS 2 - Exactly Once";
    mqtt_publish_opts_t pub_opts = {0};
    pub_opts.topic = topic;
    pub_opts.payload = (const uint8_t *)payload;
    pub_opts.payload_len = strlen(payload);
    pub_opts.qos = MQTT_QOS_2;
    pub_opts.retain = false;

    err = mqtt_publish_async(g_client, &pub_opts, NULL);
    ASSERT_EQ(MQTT_OK, err);

    /* Wait for PUBCOMP */
    ASSERT_TRUE(wait_for_condition(&g_published, OPERATION_TIMEOUT_MS));

    /* Wait for message */
    ASSERT_TRUE(wait_for_condition(&g_message_received, OPERATION_TIMEOUT_MS));

    /* Verify message content */
    ASSERT_STR_EQ(topic, g_received_topic);
    ASSERT_STR_EQ(payload, g_received_payload);
}

/*******************************************************************************
 * Tests - Wildcards
 ******************************************************************************/

TEST(subscribe_wildcard_plus)
{
    char client_id[64];
    char base_topic[128];
    char sub_topic[160];
    char pub_topic[160];

    generate_client_id(client_id, sizeof(client_id));
    generate_topic(base_topic, sizeof(base_topic));
    snprintf(sub_topic, sizeof(sub_topic), "%s/+/sensor", base_topic);
    snprintf(pub_topic, sizeof(pub_topic), "%s/room1/sensor", base_topic);

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.transport_type = MQTT_TRANSPORT_TCP;

    g_client = mqtt_client_create(&config);
    ASSERT_NOT_NULL(g_client);

    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_disconnect = on_disconnect;
    callbacks.on_subscribe = on_subscribe;
    callbacks.on_message = on_message;
    mqtt_set_callbacks(g_client, &callbacks);

    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = MOSQUITTO_HOST;
    conn_opts.port = MOSQUITTO_PORT;
    conn_opts.client_id = client_id;
    conn_opts.keepalive_sec = 60;
    conn_opts.clean_session = true;

    mqtt_error_t err = mqtt_connect_async(g_client, &conn_opts);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_connected, CONNECT_TIMEOUT_MS));

    /* Subscribe with + wildcard */
    mqtt_subscribe_opts_t sub_opts = {0};
    sub_opts.topic_filter = sub_topic;
    sub_opts.max_qos = MQTT_QOS_1;

    err = mqtt_subscribe_async(g_client, &sub_opts, 1, NULL);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_subscribed, OPERATION_TIMEOUT_MS));

    /* Publish to matching topic */
    const char *payload = "Wildcard test";
    mqtt_publish_opts_t pub_opts = {0};
    pub_opts.topic = pub_topic;
    pub_opts.payload = (const uint8_t *)payload;
    pub_opts.payload_len = strlen(payload);
    pub_opts.qos = MQTT_QOS_1;
    pub_opts.retain = false;

    err = mqtt_publish_async(g_client, &pub_opts, NULL);
    ASSERT_EQ(MQTT_OK, err);

    /* Wait for message */
    ASSERT_TRUE(wait_for_condition(&g_message_received, OPERATION_TIMEOUT_MS));
    ASSERT_STR_EQ(pub_topic, g_received_topic);
}

TEST(subscribe_wildcard_hash)
{
    char client_id[64];
    char base_topic[128];
    char sub_topic[160];
    char pub_topic[192];

    generate_client_id(client_id, sizeof(client_id));
    generate_topic(base_topic, sizeof(base_topic));
    snprintf(sub_topic, sizeof(sub_topic), "%s/#", base_topic);
    snprintf(pub_topic, sizeof(pub_topic), "%s/deep/nested/topic", base_topic);

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.transport_type = MQTT_TRANSPORT_TCP;

    g_client = mqtt_client_create(&config);
    ASSERT_NOT_NULL(g_client);

    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_disconnect = on_disconnect;
    callbacks.on_subscribe = on_subscribe;
    callbacks.on_message = on_message;
    mqtt_set_callbacks(g_client, &callbacks);

    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = MOSQUITTO_HOST;
    conn_opts.port = MOSQUITTO_PORT;
    conn_opts.client_id = client_id;
    conn_opts.keepalive_sec = 60;
    conn_opts.clean_session = true;

    mqtt_error_t err = mqtt_connect_async(g_client, &conn_opts);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_connected, CONNECT_TIMEOUT_MS));

    /* Subscribe with # wildcard */
    mqtt_subscribe_opts_t sub_opts = {0};
    sub_opts.topic_filter = sub_topic;
    sub_opts.max_qos = MQTT_QOS_1;

    err = mqtt_subscribe_async(g_client, &sub_opts, 1, NULL);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_subscribed, OPERATION_TIMEOUT_MS));

    /* Publish to matching topic */
    const char *payload = "Multi-level wildcard";
    mqtt_publish_opts_t pub_opts = {0};
    pub_opts.topic = pub_topic;
    pub_opts.payload = (const uint8_t *)payload;
    pub_opts.payload_len = strlen(payload);
    pub_opts.qos = MQTT_QOS_1;
    pub_opts.retain = false;

    err = mqtt_publish_async(g_client, &pub_opts, NULL);
    ASSERT_EQ(MQTT_OK, err);

    /* Wait for message */
    ASSERT_TRUE(wait_for_condition(&g_message_received, OPERATION_TIMEOUT_MS));
    ASSERT_STR_EQ(pub_topic, g_received_topic);
}

/*******************************************************************************
 * Tests - Retained Messages
 ******************************************************************************/

TEST(retained_message)
{
    char client_id[64];
    char topic[128];

    generate_client_id(client_id, sizeof(client_id));
    generate_topic(topic, sizeof(topic));

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.transport_type = MQTT_TRANSPORT_TCP;

    g_client = mqtt_client_create(&config);
    ASSERT_NOT_NULL(g_client);

    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_disconnect = on_disconnect;
    callbacks.on_subscribe = on_subscribe;
    callbacks.on_message = on_message;
    mqtt_set_callbacks(g_client, &callbacks);

    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = MOSQUITTO_HOST;
    conn_opts.port = MOSQUITTO_PORT;
    conn_opts.client_id = client_id;
    conn_opts.keepalive_sec = 60;
    conn_opts.clean_session = true;

    mqtt_error_t err = mqtt_connect_async(g_client, &conn_opts);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_connected, CONNECT_TIMEOUT_MS));

    /* Publish retained message */
    const char *payload = "Retained message content";
    mqtt_publish_opts_t pub_opts = {0};
    pub_opts.topic = topic;
    pub_opts.payload = (const uint8_t *)payload;
    pub_opts.payload_len = strlen(payload);
    pub_opts.qos = MQTT_QOS_1;
    pub_opts.retain = true;

    err = mqtt_publish_async(g_client, &pub_opts, NULL);
    ASSERT_EQ(MQTT_OK, err);

    /* Give broker time to store */
    usleep(500000);
    mqtt_loop(g_client, 100);

    /* Subscribe - should receive retained message */
    mqtt_subscribe_opts_t sub_opts = {0};
    sub_opts.topic_filter = topic;
    sub_opts.max_qos = MQTT_QOS_1;

    err = mqtt_subscribe_async(g_client, &sub_opts, 1, NULL);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_subscribed, OPERATION_TIMEOUT_MS));
    ASSERT_TRUE(wait_for_condition(&g_message_received, OPERATION_TIMEOUT_MS));

    ASSERT_STR_EQ(topic, g_received_topic);
    ASSERT_STR_EQ(payload, g_received_payload);

    /* Clean up retained message by publishing empty payload */
    mqtt_publish_opts_t clear_opts = {0};
    clear_opts.topic = topic;
    clear_opts.payload = NULL;
    clear_opts.payload_len = 0;
    clear_opts.qos = MQTT_QOS_1;
    clear_opts.retain = true;

    err = mqtt_publish_async(g_client, &clear_opts, NULL);
    ASSERT_EQ(MQTT_OK, err);
}

/*******************************************************************************
 * Tests - Unsubscribe
 ******************************************************************************/

TEST(unsubscribe)
{
    char client_id[64];
    char topic[128];

    generate_client_id(client_id, sizeof(client_id));
    generate_topic(topic, sizeof(topic));

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.transport_type = MQTT_TRANSPORT_TCP;

    g_client = mqtt_client_create(&config);
    ASSERT_NOT_NULL(g_client);

    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_disconnect = on_disconnect;
    callbacks.on_subscribe = on_subscribe;
    callbacks.on_message = on_message;
    mqtt_set_callbacks(g_client, &callbacks);

    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = MOSQUITTO_HOST;
    conn_opts.port = MOSQUITTO_PORT;
    conn_opts.client_id = client_id;
    conn_opts.keepalive_sec = 60;
    conn_opts.clean_session = true;

    mqtt_error_t err = mqtt_connect_async(g_client, &conn_opts);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_connected, CONNECT_TIMEOUT_MS));

    /* Subscribe */
    mqtt_subscribe_opts_t sub_opts = {0};
    sub_opts.topic_filter = topic;
    sub_opts.max_qos = MQTT_QOS_1;

    err = mqtt_subscribe_async(g_client, &sub_opts, 1, NULL);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_subscribed, OPERATION_TIMEOUT_MS));

    /* Publish - should receive */
    const char *payload1 = "Before unsubscribe";
    mqtt_publish_opts_t pub_opts = {0};
    pub_opts.topic = topic;
    pub_opts.payload = (const uint8_t *)payload1;
    pub_opts.payload_len = strlen(payload1);
    pub_opts.qos = MQTT_QOS_1;
    pub_opts.retain = false;

    err = mqtt_publish_async(g_client, &pub_opts, NULL);
    ASSERT_EQ(MQTT_OK, err);

    ASSERT_TRUE(wait_for_condition(&g_message_received, OPERATION_TIMEOUT_MS));

    /* Unsubscribe */
    reset_state();
    const char *topic_filters[] = { topic };
    err = mqtt_unsubscribe_async(g_client, topic_filters, 1, NULL);
    ASSERT_EQ(MQTT_OK, err);

    usleep(500000);  /* Wait for UNSUBACK */
    mqtt_loop(g_client, 100);

    /* Publish again - should NOT receive */
    const char *payload2 = "After unsubscribe";
    pub_opts.payload = (const uint8_t *)payload2;
    pub_opts.payload_len = strlen(payload2);

    err = mqtt_publish_async(g_client, &pub_opts, NULL);
    ASSERT_EQ(MQTT_OK, err);

    /* Wait and verify no message received */
    wait_for_condition(&g_message_received, 2000);
    ASSERT_FALSE(g_message_received);
}

/*******************************************************************************
 * Main
 ******************************************************************************/

int main(void)
{
    /* Ignore SIGPIPE for socket handling */
    signal(SIGPIPE, SIG_IGN);

    /* Initialize library */
    mqtt_error_t err = mqtt_lib_init();
    if (err != MQTT_OK) {
        fprintf(stderr, "Failed to initialize MQTT library: %s\n", mqtt_error_str(err));
        return 1;
    }

    TEST_SUITE_BEGIN("MQTT Integration Tests");

    SET_SETUP(test_setup);
    SET_TEARDOWN(test_teardown);

    printf("\n--- Basic Connectivity ---\n");
    RUN_TEST_WITH_FIXTURE(connect_mosquitto);
    RUN_TEST_WITH_FIXTURE(connect_hivemq);

    printf("\n--- Publish/Subscribe ---\n");
    RUN_TEST_WITH_FIXTURE(pubsub_qos0);
    RUN_TEST_WITH_FIXTURE(pubsub_qos1);
    RUN_TEST_WITH_FIXTURE(pubsub_qos2);

    printf("\n--- Wildcards ---\n");
    RUN_TEST_WITH_FIXTURE(subscribe_wildcard_plus);
    RUN_TEST_WITH_FIXTURE(subscribe_wildcard_hash);

    printf("\n--- Advanced Features ---\n");
    RUN_TEST_WITH_FIXTURE(retained_message);
    RUN_TEST_WITH_FIXTURE(unsubscribe);

    mqtt_lib_cleanup();

    TEST_SUITE_END();
}
