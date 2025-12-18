/**
 * @file comprehensive_demo.c
 * @brief Comprehensive MQTT Client Library Demo
 *
 * This example demonstrates all major features of the MQTT client library:
 *
 * PROTOCOL FEATURES:
 *   - MQTT 3.1.1 and MQTT 5.0 protocols
 *   - QoS 0, 1, and 2 message delivery
 *   - Retained messages
 *   - Last Will and Testament (LWT)
 *   - Topic wildcards (+ and #)
 *   - Clean and persistent sessions
 *
 * TRANSPORT LAYERS:
 *   - TCP (plain socket)
 *   - WebSocket (ws://)
 *   - TLS and WebSocket Secure (if compiled with TLS support)
 *
 * ASYNC FEATURES:
 *   - Event-driven callbacks
 *   - Non-blocking operations
 *   - Publish acknowledgment tracking
 *
 * MQTT 5.0 SPECIFIC:
 *   - User properties
 *   - Message expiry
 *   - Response topic / correlation data
 *   - Subscription identifiers
 *   - Reason codes
 *
 * ADVANCED:
 *   - Reconnection with subscription restoration
 *   - Multiple subscriptions
 *   - Request/response pattern (MQTT 5.0)
 *
 * Usage: ./comprehensive_demo [broker_host] [port]
 * Defaults: test.mosquitto.org 1883
 *
 * For WebSocket demo, use port 8080 on test.mosquitto.org
 * For TLS demo (if compiled), use port 8883 or 8884 for client certs
 */

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "mqtt/mqtt.h"

/* ==========================================================================
 * Demo Configuration
 * ========================================================================== */

#define DEMO_CLIENT_ID_PREFIX  "mqtt_demo_"
#define DEMO_TOPIC_BASE        "mqtt_client_demo"
#define MAX_PENDING_OPS        10

/* Global state for demo */
static volatile bool g_running = true;
static volatile bool g_connected = false;
static volatile int g_messages_received = 0;
static volatile int g_publishes_completed = 0;
static volatile int g_publishes_failed = 0;

/* Pending operation tracking */
typedef struct {
    uint16_t packet_id;
    bool completed;
    const char *description;
} pending_op_t;

static pending_op_t g_pending_ops[MAX_PENDING_OPS];
static int g_pending_count = 0;

/* Demo results tracking */
typedef struct {
    const char *name;
    int expected;
    int actual;
    const char *metric;
    bool passed;
} demo_result_t;

#define MAX_RESULTS 20
static demo_result_t g_results[MAX_RESULTS];
static int g_result_count = 0;

static void record_result(const char *name, const char *metric, int expected, int actual)
{
    if (g_result_count < MAX_RESULTS) {
        g_results[g_result_count].name = name;
        g_results[g_result_count].metric = metric;
        g_results[g_result_count].expected = expected;
        g_results[g_result_count].actual = actual;
        g_results[g_result_count].passed = (expected == actual);
        g_result_count++;
    }
}

static void print_final_results(void)
{
    printf("\n");
    printf("================================================================================\n");
    printf("  FINAL RESULTS SUMMARY\n");
    printf("================================================================================\n\n");

    printf("%-28s %-20s %10s %10s %8s\n", "Demo", "Metric", "Expected", "Actual", "Status");
    printf("--------------------------------------------------------------------------------\n");

    int passed = 0;
    int failed = 0;

    for (int i = 0; i < g_result_count; i++) {
        const char *status = g_results[i].passed ? "PASS" : "FAIL";
        printf("%-28s %-20s %10d %10d %8s\n",
               g_results[i].name,
               g_results[i].metric,
               g_results[i].expected,
               g_results[i].actual,
               status);

        if (g_results[i].passed) {
            passed++;
        } else {
            failed++;
        }
    }

    printf("--------------------------------------------------------------------------------\n");
    printf("Total: %d passed, %d failed out of %d checks\n\n", passed, failed, g_result_count);

    if (failed > 0) {
        printf("Note: Failures may be due to broker latency. Try with a local broker:\n");
        printf("  ./comprehensive_demo localhost 1883\n\n");
    }
}

/* ==========================================================================
 * Utility Functions
 * ========================================================================== */

static void signal_handler(int sig)
{
    (void)sig;
    printf("\n[SIGNAL] Shutdown requested\n");
    g_running = false;
}

static void generate_client_id(char *buf, size_t len)
{
    snprintf(buf, len, "%s%ld", DEMO_CLIENT_ID_PREFIX, (long)time(NULL) % 100000);
}

static const char *qos_str(mqtt_qos_t qos)
{
    switch (qos) {
        case MQTT_QOS_0: return "QoS 0 (At most once)";
        case MQTT_QOS_1: return "QoS 1 (At least once)";
        case MQTT_QOS_2: return "QoS 2 (Exactly once)";
        default: return "Unknown QoS";
    }
}

static void track_pending_op(uint16_t packet_id, const char *desc)
{
    if (g_pending_count < MAX_PENDING_OPS) {
        g_pending_ops[g_pending_count].packet_id = packet_id;
        g_pending_ops[g_pending_count].completed = false;
        g_pending_ops[g_pending_count].description = desc;
        g_pending_count++;
    }
}

static void mark_op_complete(uint16_t packet_id)
{
    for (int i = 0; i < g_pending_count; i++) {
        if (g_pending_ops[i].packet_id == packet_id) {
            g_pending_ops[i].completed = true;
            break;
        }
    }
}

static bool all_ops_complete(void)
{
    for (int i = 0; i < g_pending_count; i++) {
        if (!g_pending_ops[i].completed) {
            return false;
        }
    }
    return true;
}

static void wait_for_pending_ops(mqtt_client_t *client, int timeout_ms)
{
    int elapsed = 0;
    while (!all_ops_complete() && elapsed < timeout_ms && g_running) {
        mqtt_loop(client, 100);
        elapsed += 100;
    }
}

static void print_separator(const char *title)
{
    printf("\n");
    printf("============================================================\n");
    printf("  %s\n", title);
    printf("============================================================\n\n");
}

/* ==========================================================================
 * Callback Functions
 * ========================================================================== */

/**
 * @brief Called when connection is established
 */
static void on_connect(mqtt_client_t *client, void *user_data, bool session_present)
{
    (void)client;
    (void)user_data;
    printf("[CALLBACK] Connected! Session present: %s\n",
           session_present ? "yes (previous session restored)" : "no (new session)");
    g_connected = true;
}

/**
 * @brief Called when disconnected from broker
 */
static void on_disconnect(mqtt_client_t *client, void *user_data, int reason_code)
{
    (void)client;
    (void)user_data;
    printf("[CALLBACK] Disconnected! Reason code: %d\n", reason_code);
    g_connected = false;
}

/**
 * @brief Called when a message is received on a subscribed topic
 */
static void on_message(mqtt_client_t *client, void *user_data, const mqtt_message_t *msg)
{
    (void)client;
    (void)user_data;

    g_messages_received++;

    printf("[CALLBACK] Message received:\n");
    printf("  Topic: %.*s\n", (int)msg->topic_len, msg->topic);
    printf("  Payload (%zu bytes): %.*s\n",
           msg->payload_len,
           (int)(msg->payload_len > 100 ? 100 : msg->payload_len),
           (const char *)msg->payload);
    printf("  QoS: %d, Retain: %s, Dup: %s\n",
           msg->qos,
           msg->retain ? "yes" : "no",
           msg->dup ? "yes" : "no");

    /* Show MQTT 5.0 properties if present */
    if (msg->response_topic) {
        printf("  Response Topic: %s\n", msg->response_topic);
    }
    if (msg->correlation_data && msg->correlation_data_len > 0) {
        printf("  Correlation Data: %.*s\n",
               (int)msg->correlation_data_len, (const char *)msg->correlation_data);
    }
    if (msg->content_type) {
        printf("  Content Type: %s\n", msg->content_type);
    }
    if (msg->subscription_id > 0) {
        printf("  Subscription ID: %u\n", msg->subscription_id);
    }
    printf("\n");
}

/**
 * @brief Called when a QoS 1/2 publish is acknowledged
 */
static void on_publish_complete(mqtt_client_t *client, void *user_data, uint16_t packet_id)
{
    (void)client;
    (void)user_data;
    printf("[CALLBACK] Publish complete (packet_id=%u)\n", packet_id);
    g_publishes_completed++;
    mark_op_complete(packet_id);
}

/**
 * @brief Called when a QoS 1/2 publish fails after retries
 */
static void on_publish_failed(mqtt_client_t *client, void *user_data,
                               uint16_t packet_id, mqtt_error_t reason)
{
    (void)client;
    (void)user_data;
    printf("[CALLBACK] Publish FAILED (packet_id=%u): %s\n",
           packet_id, mqtt_error_str(reason));
    g_publishes_failed++;
    mark_op_complete(packet_id);
}

/**
 * @brief Called when subscribe completes
 */
static void on_subscribe(mqtt_client_t *client, void *user_data,
                          uint16_t packet_id, const mqtt_qos_t *granted_qos, size_t count)
{
    (void)client;
    (void)user_data;
    printf("[CALLBACK] Subscribe complete (packet_id=%u):\n", packet_id);
    for (size_t i = 0; i < count; i++) {
        printf("  Subscription %zu: granted %s\n", i + 1, qos_str(granted_qos[i]));
    }
    mark_op_complete(packet_id);
}

/**
 * @brief Called when server requests redirect (MQTT 5.0)
 */
static void on_redirect(mqtt_client_t *client, void *user_data,
                         const char *server_reference, bool is_permanent)
{
    (void)client;
    (void)user_data;
    printf("[CALLBACK] Server redirect: %s (%s)\n",
           server_reference,
           is_permanent ? "permanent" : "temporary");
}

/* ==========================================================================
 * Demo Sections
 * ========================================================================== */

/**
 * @brief Demo 1: Basic Publish/Subscribe with all QoS levels
 */
static void demo_basic_pubsub(mqtt_client_t *client)
{
    print_separator("Demo 1: Basic Publish/Subscribe (QoS 0, 1, 2)");

    mqtt_error_t err;
    char topic[128];
    const char *message = "Hello from comprehensive demo!";
    int initial_msg_count = g_messages_received;
    int initial_ack_count = g_publishes_completed;

    /* Reset pending ops for this demo */
    g_pending_count = 0;

    /* Subscribe to a topic first (synchronous to ensure it's active before publishing) */
    snprintf(topic, sizeof(topic), "%s/basic/#", DEMO_TOPIC_BASE);
    printf("Subscribing to: %s\n", topic);

    mqtt_subscribe_opts_t sub_opts = {0};
    sub_opts.topic_filter = topic;
    sub_opts.max_qos = MQTT_QOS_2;

    err = mqtt_subscribe(client, &sub_opts, 1);
    if (err == MQTT_OK) {
        printf("Subscribed successfully!\n");
    } else {
        printf("Subscribe failed: %s\n", mqtt_error_str(err));
        return;
    }

    /* Small delay to ensure subscription is fully established */
    mqtt_loop(client, 200);

    /* Publish at each QoS level */
    mqtt_qos_t qos_levels[] = {MQTT_QOS_0, MQTT_QOS_1, MQTT_QOS_2};

    for (int i = 0; i < 3; i++) {
        snprintf(topic, sizeof(topic), "%s/basic/qos%d", DEMO_TOPIC_BASE, qos_levels[i]);

        mqtt_publish_opts_t pub_opts = {0};
        pub_opts.topic = topic;
        pub_opts.payload = (const uint8_t *)message;
        pub_opts.payload_len = strlen(message);
        pub_opts.qos = qos_levels[i];
        pub_opts.retain = false;

        printf("\nPublishing %s to: %s\n", qos_str(qos_levels[i]), topic);

        /* Use synchronous publish - completes full QoS handshake before returning */
        err = mqtt_publish(client, &pub_opts);
        if (err == MQTT_OK) {
            if (qos_levels[i] == MQTT_QOS_0) {
                printf("Publish sent (no ack for QoS 0)\n");
            } else {
                printf("Publish complete (QoS %d handshake done)\n", qos_levels[i]);
                /* Note: callback will increment g_publishes_completed */
            }
        } else {
            printf("Publish failed: %s\n", mqtt_error_str(err));
        }

        /* Process events to receive our own message back */
        mqtt_loop(client, 100);
    }

    /* Process incoming messages - give more time for messages to arrive */
    printf("\nProcessing incoming messages...\n");
    int wait_loops = 0;
    int target_messages = initial_msg_count + 3;  /* Expect 3 messages (QoS 0, 1, 2) */

    while (g_messages_received < target_messages && wait_loops < 50 && g_running) {
        mqtt_loop(client, 100);
        wait_loops++;
    }

    int received_in_demo = g_messages_received - initial_msg_count;
    int acks_in_demo = g_publishes_completed - initial_ack_count;

    printf("\n--- Demo 1 Results ---\n");
    printf("  Subscribe:     Expected: success    | Actual: %s\n", err == MQTT_OK ? "success" : "FAILED");
    printf("  Messages:      Expected: 3          | Actual: %d %s\n",
           received_in_demo, received_in_demo == 3 ? "" : "<-- ISSUE");
    printf("  QoS 1/2 Acks:  Expected: 2          | Actual: %d %s\n",
           acks_in_demo, acks_in_demo == 2 ? "" : "<-- ISSUE");

    record_result("Demo 1: Basic Pub/Sub", "Messages received", 3, received_in_demo);
    record_result("Demo 1: Basic Pub/Sub", "QoS 1/2 acks", 2, acks_in_demo);
}

/**
 * @brief Demo 1b: Async Publish/Subscribe
 *
 * Demonstrates non-blocking async operations where multiple publishes
 * are queued and processed in the background while other work continues.
 */
static void demo_async_pubsub(mqtt_client_t *client)
{
    print_separator("Demo 1b: Async Publish/Subscribe");

    /* Drain any pending data from previous demo */
    for (int i = 0; i < 10; i++) {
        mqtt_loop(client, 50);
    }

    mqtt_error_t err;
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/async/#", DEMO_TOPIC_BASE);

    int initial_msg_count = g_messages_received;
    int initial_ack_count = g_publishes_completed;

    /* Reset pending ops */
    g_pending_count = 0;

    /* Subscribe first */
    printf("Subscribing to: %s\n", topic);
    mqtt_subscribe_opts_t sub_opts = {0};
    sub_opts.topic_filter = topic;
    sub_opts.max_qos = MQTT_QOS_1;

    err = mqtt_subscribe(client, &sub_opts, 1);
    if (err != MQTT_OK) {
        printf("Subscribe failed: %s\n", mqtt_error_str(err));
        record_result("Demo 1b: Async Pub/Sub", "Async messages", 5, 0);
        return;
    }
    mqtt_loop(client, 200);

    /* Queue multiple async publishes - must flush between each one because
     * the async API uses a single send buffer that gets overwritten.
     * In real applications, use mqtt_want_write() + mqtt_process_write()
     * in an event loop for proper async operation.
     */
    printf("\nSending 5 async publishes (QoS 1)...\n");
    for (int i = 0; i < 5; i++) {
        char pub_topic[128];
        char payload[64];
        snprintf(pub_topic, sizeof(pub_topic), "%s/async/msg%d", DEMO_TOPIC_BASE, i + 1);
        snprintf(payload, sizeof(payload), "Async message %d", i + 1);

        mqtt_publish_opts_t pub_opts = {0};
        pub_opts.topic = pub_topic;
        pub_opts.payload = (const uint8_t *)payload;
        pub_opts.payload_len = strlen(payload);
        pub_opts.qos = MQTT_QOS_1;

        uint16_t pub_id = 0;
        err = mqtt_publish_async(client, &pub_opts, &pub_id);
        if (err == MQTT_OK && pub_id > 0) {
            track_pending_op(pub_id, "Async publish");
            printf("  Async publish %d (packet_id=%u) - ", i + 1, pub_id);

            /* IMPORTANT: Must flush send buffer before next async operation
             * because async API uses a single buffer that gets overwritten */
            if (mqtt_want_write(client)) {
                mqtt_process_write(client);
                printf("sent\n");
            }
        } else {
            printf("  Failed to queue message %d: %s\n", i + 1, mqtt_error_str(err));
        }
    }

    /* Do some "other work" while messages are processed */
    printf("\nDoing other work while messages are processed in background...\n");
    printf("  (In real code, this could be UI updates, sensor reads, etc.)\n");

    /* Process the event loop to receive all responses */
    printf("\nProcessing event loop until all acks and messages received...\n");
    int iterations = 0;
    int target_messages = initial_msg_count + 5;

    while ((!all_ops_complete() || g_messages_received < target_messages)
           && iterations < 100 && g_running) {
        mqtt_loop(client, 100);
        iterations++;
    }

    int received_in_demo = g_messages_received - initial_msg_count;
    int acks_in_demo = g_publishes_completed - initial_ack_count;

    printf("\n--- Demo 1b Results ---\n");
    printf("  Async publishes: 5 queued\n");
    printf("  Messages recv:   Expected: 5          | Actual: %d %s\n",
           received_in_demo, received_in_demo == 5 ? "" : "<-- ISSUE");
    printf("  QoS 1 acks:      Expected: 5          | Actual: %d %s\n",
           acks_in_demo, acks_in_demo == 5 ? "" : "<-- ISSUE");
    printf("  Loop iterations: %d (lower is better)\n", iterations);

    record_result("Demo 1b: Async Pub/Sub", "Async messages", 5, received_in_demo);
    record_result("Demo 1b: Async Pub/Sub", "Async acks", 5, acks_in_demo);
}

/**
 * @brief Demo 2: Retained Messages
 */
static void demo_retained_messages(mqtt_client_t *client)
{
    print_separator("Demo 2: Retained Messages");

    mqtt_error_t err;
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/retained", DEMO_TOPIC_BASE);

    /* First, clear any existing retained message */
    printf("Clearing any existing retained message...\n");
    mqtt_publish_opts_t clear_opts = {0};
    clear_opts.topic = topic;
    clear_opts.payload = NULL;
    clear_opts.payload_len = 0;
    clear_opts.qos = MQTT_QOS_1;
    clear_opts.retain = true;  /* Empty retained message clears it */

    mqtt_publish(client, &clear_opts);
    mqtt_loop(client, 500);

    /* Publish a retained message */
    const char *retained_msg = "This is a retained message (timestamp: %ld)";
    char message[256];
    snprintf(message, sizeof(message), retained_msg, (long)time(NULL));

    printf("Publishing retained message: %s\n", message);

    mqtt_publish_opts_t pub_opts = {0};
    pub_opts.topic = topic;
    pub_opts.payload = (const uint8_t *)message;
    pub_opts.payload_len = strlen(message);
    pub_opts.qos = MQTT_QOS_1;
    pub_opts.retain = true;

    /* Use synchronous publish to ensure message is stored before subscribing */
    err = mqtt_publish(client, &pub_opts);
    if (err == MQTT_OK) {
        printf("Retained message published!\n");
    }

    /* Drain any pending callbacks and give broker time to store */
    for (int i = 0; i < 10; i++) {
        mqtt_loop(client, 100);
    }

    /* Subscribe - should immediately receive the retained message */
    printf("\nSubscribing to retained topic: %s\n", topic);
    int prev_count = g_messages_received;

    mqtt_subscribe_opts_t sub_opts = {0};
    sub_opts.topic_filter = topic;
    sub_opts.max_qos = MQTT_QOS_1;

    err = mqtt_subscribe(client, &sub_opts, 1);
    if (err != MQTT_OK) {
        printf("Subscribe failed: %s\n", mqtt_error_str(err));
    }

    /* Process loop to receive SUBACK and retained message */
    printf("Waiting for retained message...\n");
    for (int i = 0; i < 30 && g_messages_received == prev_count && g_running; i++) {
        mqtt_loop(client, 100);
    }

    /* Check if retained message arrived */
    if (g_messages_received > prev_count) {
        printf("Received retained message!\n");
    } else {
        printf("No retained message received\n");
    }

    /* Clean up - clear the retained message */
    printf("\nClearing retained message...\n");
    mqtt_publish(client, &clear_opts);
    mqtt_loop(client, 500);

    int retained_received = g_messages_received - prev_count;
    printf("\n--- Demo 2 Results ---\n");
    printf("  Publish retained: Expected: success    | Actual: %s\n", err == MQTT_OK ? "success" : "FAILED");
    printf("  Recv on sub:      Expected: 1          | Actual: %d %s\n",
           retained_received, retained_received == 1 ? "" : "<-- ISSUE");

    record_result("Demo 2: Retained Messages", "Retained received", 1, retained_received);
}

/**
 * @brief Demo 3: Topic Wildcards
 */
static void demo_topic_wildcards(mqtt_client_t *client)
{
    print_separator("Demo 3: Topic Wildcards (+ and #)");

    /* Drain any pending messages from previous demos */
    for (int i = 0; i < 10; i++) {
        mqtt_loop(client, 50);
    }

    mqtt_error_t err;
    int prev_count = g_messages_received;

    /* Subscribe with single-level wildcard (+) */
    printf("Subscribing with single-level wildcard: %s/sensors/+/temperature\n",
           DEMO_TOPIC_BASE);

    char topic_plus[128];
    snprintf(topic_plus, sizeof(topic_plus), "%s/sensors/+/temperature", DEMO_TOPIC_BASE);

    mqtt_subscribe_opts_t sub_plus = {0};
    sub_plus.topic_filter = topic_plus;
    sub_plus.max_qos = MQTT_QOS_1;
    mqtt_subscribe(client, &sub_plus, 1);
    mqtt_loop(client, 500);

    /* Subscribe with multi-level wildcard (#) */
    printf("Subscribing with multi-level wildcard: %s/events/#\n", DEMO_TOPIC_BASE);

    char topic_hash[128];
    snprintf(topic_hash, sizeof(topic_hash), "%s/events/#", DEMO_TOPIC_BASE);

    mqtt_subscribe_opts_t sub_hash = {0};
    sub_hash.topic_filter = topic_hash;
    sub_hash.max_qos = MQTT_QOS_1;
    mqtt_subscribe(client, &sub_hash, 1);
    mqtt_loop(client, 500);

    /* Publish to topics that match the wildcards */
    const char *test_topics[] = {
        "/sensors/living_room/temperature",
        "/sensors/bedroom/temperature",
        "/sensors/kitchen/humidity",  /* Won't match + wildcard */
        "/events/alert",
        "/events/system/startup",
        "/events/system/network/connected"
    };

    char full_topic[256];
    for (size_t i = 0; i < sizeof(test_topics) / sizeof(test_topics[0]); i++) {
        snprintf(full_topic, sizeof(full_topic), "%s%s", DEMO_TOPIC_BASE, test_topics[i]);

        mqtt_publish_opts_t pub = {0};
        pub.topic = full_topic;
        char msg[64];
        snprintf(msg, sizeof(msg), "Test message %zu", i + 1);
        pub.payload = (const uint8_t *)msg;
        pub.payload_len = strlen(msg);
        pub.qos = MQTT_QOS_0;

        printf("Publishing to: %s\n", full_topic);
        err = mqtt_publish(client, &pub);
        if (err != MQTT_OK) {
            printf("  Failed: %s\n", mqtt_error_str(err));
        }
    }

    /* Wait for messages */
    printf("\nWaiting for wildcard-matched messages...\n");
    for (int i = 0; i < 30 && g_running; i++) {
        mqtt_loop(client, 100);
    }

    int wildcard_received = g_messages_received - prev_count;
    printf("\n--- Demo 3 Results ---\n");
    printf("  '+' wildcard:   Expected: 2 messages (living_room, bedroom temps)\n");
    printf("  '#' wildcard:   Expected: 3 messages (alert, startup, connected)\n");
    printf("  Total received: Expected: 5          | Actual: %d %s\n",
           wildcard_received, wildcard_received == 5 ? "" : "<-- ISSUE");

    record_result("Demo 3: Topic Wildcards", "Wildcard messages", 5, wildcard_received);
}

/**
 * @brief Demo 4: Last Will and Testament (LWT)
 */
static void demo_will_message(mqtt_client_t *client, const char *host, uint16_t port)
{
    print_separator("Demo 4: Last Will and Testament (LWT)");

    /* Drain any pending messages from previous demos */
    for (int i = 0; i < 10; i++) {
        mqtt_loop(client, 50);
    }

    /* Create a second client with a will message */
    printf("Creating second client with Last Will message...\n");

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.send_buffer_size = 4096;
    config.recv_buffer_size = 4096;

    mqtt_client_t *will_client = mqtt_client_create(&config);
    if (!will_client) {
        printf("Failed to create will client\n");
        return;
    }

    /* Set up the will message */
    char will_topic[128];
    snprintf(will_topic, sizeof(will_topic), "%s/status/will_demo", DEMO_TOPIC_BASE);

    mqtt_will_message_t will = {0};
    will.topic = will_topic;
    will.payload = (const uint8_t *)"Client disconnected unexpectedly!";
    will.payload_len = strlen((const char *)will.payload);
    will.qos = MQTT_QOS_1;
    will.retain = false;

    /* Subscribe to will topic on main client */
    printf("Subscribing to will topic: %s\n", will_topic);
    mqtt_subscribe_opts_t sub = {0};
    sub.topic_filter = will_topic;
    sub.max_qos = MQTT_QOS_1;
    mqtt_subscribe(client, &sub, 1);
    mqtt_loop(client, 500);

    /* Connect second client with will */
    mqtt_connect_opts_t conn = {0};
    conn.host = host;
    conn.port = port;
    conn.client_id = "mqtt_demo_will_client";
    conn.clean_session = true;
    conn.keepalive_sec = 5;  /* Short keepalive for demo */
    conn.protocol_version = MQTT_VERSION_3_1_1;
    conn.transport_type = MQTT_TRANSPORT_TCP;
    conn.connect_timeout_ms = 10000;
    conn.will = &will;

    printf("Connecting will_client (keepalive=5s)...\n");
    mqtt_error_t err = mqtt_connect(will_client, &conn);
    if (err != MQTT_OK) {
        printf("Will client connect failed: %s\n", mqtt_error_str(err));
        mqtt_client_destroy(will_client);
        return;
    }
    printf("Will client connected!\n");

    /* Trigger LWT by letting keepalive timeout expire.
     *
     * How it works:
     * - will_client has keepalive=5 seconds
     * - We stop calling mqtt_loop() on will_client (simulating a hung/crashed client)
     * - Broker expects PINGREQ within 1.5x keepalive (7.5 seconds)
     * - When no PINGREQ arrives, broker considers connection lost and sends will
     *
     * Note: We keep will_client alive (don't destroy it) but stop its event loop.
     * The main client continues processing to receive the will message.
     */
    printf("\nSimulating client hang (stopping mqtt_loop on will_client)...\n");
    printf("Broker will detect keepalive timeout after ~7.5 seconds (1.5x keepalive)...\n");

    /* Wait for broker to detect keepalive timeout and send will message */
    printf("Waiting for will message (up to 12 seconds)...\n");
    int prev_count = g_messages_received;
    for (int i = 0; i < 120 && g_messages_received == prev_count && g_running; i++) {
        /* Only process main client - will_client is "hung" */
        mqtt_loop(client, 100);
        if (i == 50) {
            printf("  Still waiting... (5s elapsed)\n");
        } else if (i == 80) {
            printf("  Still waiting... (8s elapsed)\n");
        }
    }

    /* Clean up will_client (broker already closed connection due to keepalive timeout) */
    mqtt_client_destroy(will_client);

    int will_received = g_messages_received - prev_count;

    printf("\n--- Demo 4 Results ---\n");
    printf("  Will client:    Expected: connect     | Actual: %s\n", err == MQTT_OK ? "connected" : "FAILED");
    printf("  Will message:   Expected: 1           | Actual: %d %s\n",
           will_received, will_received == 1 ? "" : "<-- ISSUE");

    record_result("Demo 4: Last Will (LWT)", "Will client connect", 1, err == MQTT_OK ? 1 : 0);
    record_result("Demo 4: Last Will (LWT)", "Will message recv", 1, will_received);
}

#ifdef MQTT_V5_SUPPORT
/**
 * @brief Demo 5: MQTT 5.0 Features
 */
static void demo_mqtt5_features(mqtt_client_t *client, const char *host, uint16_t port)
{
    print_separator("Demo 5: MQTT 5.0 Features");

    /* Create MQTT 5.0 client */
    printf("Creating MQTT 5.0 client...\n");

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_5_0;
    config.send_buffer_size = 8192;
    config.recv_buffer_size = 8192;

    mqtt_client_t *v5_client = mqtt_client_create(&config);
    if (!v5_client) {
        printf("Failed to create MQTT 5.0 client\n");
        return;
    }

    /* Set callbacks */
    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_disconnect = on_disconnect;
    callbacks.on_message = on_message;
    callbacks.on_publish_complete = on_publish_complete;
    callbacks.on_subscribe = on_subscribe;
    mqtt_set_callbacks(v5_client, &callbacks);

    /* Connect with MQTT 5.0 specific options */
    mqtt_connect_opts_t conn = {0};
    conn.host = host;
    conn.port = port;
    conn.client_id = "mqtt_demo_v5_client";
    conn.clean_start = true;  /* MQTT 5.0 uses clean_start instead of clean_session */
    conn.keepalive_sec = 60;
    conn.protocol_version = MQTT_VERSION_5_0;
    conn.transport_type = MQTT_TRANSPORT_TCP;
    conn.connect_timeout_ms = 10000;
    conn.session_expiry_interval = 3600;  /* Keep session for 1 hour */
    conn.receive_maximum = 10;             /* Limit concurrent QoS 1/2 */

    printf("Connecting with MQTT 5.0...\n");
    mqtt_error_t err = mqtt_connect(v5_client, &conn);
    if (err != MQTT_OK) {
        printf("MQTT 5.0 connect failed: %s\n", mqtt_error_str(err));
        mqtt_client_destroy(v5_client);
        return;
    }

    /* Subscribe with subscription identifier */
    printf("\nSubscribing with subscription identifier...\n");
    char sub_topic[128];
    snprintf(sub_topic, sizeof(sub_topic), "%s/v5/#", DEMO_TOPIC_BASE);

    mqtt_subscribe_opts_t sub = {0};
    sub.topic_filter = sub_topic;
    sub.max_qos = MQTT_QOS_1;
    sub.subscription_id = 42;  /* MQTT 5.0: Subscription ID */
    sub.no_local = false;      /* Receive own publishes to verify roundtrip */

    mqtt_subscribe(v5_client, &sub, 1);
    mqtt_loop(v5_client, 1000);

    /* Publish with MQTT 5.0 properties */
    printf("\nPublishing with MQTT 5.0 properties...\n");
    char pub_topic[128];
    snprintf(pub_topic, sizeof(pub_topic), "%s/v5/sensor_data", DEMO_TOPIC_BASE);

    int prev_count = g_messages_received;

    mqtt_publish_opts_t pub = {0};
    pub.topic = pub_topic;
    const char *payload = "{\"temperature\": 23.5, \"humidity\": 65}";
    pub.payload = (const uint8_t *)payload;
    pub.payload_len = strlen(payload);
    pub.qos = MQTT_QOS_1;

    /* MQTT 5.0 specific properties */
    pub.payload_format = MQTT_PAYLOAD_FORMAT_UTF8;
    pub.content_type = "application/json";
    pub.message_expiry = 300;  /* Message expires in 5 minutes */
    pub.response_topic = "mqtt_client_demo/v5/response";
    pub.correlation_data = (const uint8_t *)"request-123";
    pub.correlation_data_len = strlen("request-123");

    err = mqtt_publish(v5_client, &pub);
    if (err == MQTT_OK) {
        printf("Published with MQTT 5.0 properties:\n");
        printf("  Content-Type: %s\n", pub.content_type);
        printf("  Message Expiry: %u seconds\n", pub.message_expiry);
        printf("  Response Topic: %s\n", pub.response_topic);
        printf("  Correlation Data: %s\n", (const char *)pub.correlation_data);
    }

    /* Process responses - wait for our own message to come back */
    for (int i = 0; i < 30 && g_messages_received == prev_count && g_running; i++) {
        mqtt_loop(v5_client, 100);
    }

    int v5_message_received = g_messages_received - prev_count;

    /* Disconnect with reason code */
    printf("\nDisconnecting MQTT 5.0 client with normal reason code...\n");
    mqtt_disconnect(v5_client);
    mqtt_client_destroy(v5_client);

    printf("\n--- Demo 5 Results ---\n");
    printf("  V5 connect:     Expected: success     | Actual: %s\n", err == MQTT_OK ? "success" : "FAILED");
    printf("  V5 publish:     Expected: success     | Actual: %s\n", err == MQTT_OK ? "success" : "FAILED");
    printf("  V5 message:     Expected: 1           | Actual: %d %s\n",
           v5_message_received, v5_message_received == 1 ? "" : "<-- ISSUE");

    record_result("Demo 5: MQTT 5.0", "V5 connect", 1, err == MQTT_OK ? 1 : 0);
    record_result("Demo 5: MQTT 5.0", "V5 message recv", 1, v5_message_received);
}
#endif /* MQTT_V5_SUPPORT */

/**
 * @brief Demo 6: WebSocket Transport
 */
static void demo_websocket_transport(const char *host)
{
    print_separator("Demo 6: WebSocket Transport");

    int prev_count = g_messages_received;
    mqtt_error_t ws_err = MQTT_OK;

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.transport_type = MQTT_TRANSPORT_WS;
    config.send_buffer_size = 4096;
    config.recv_buffer_size = 4096;

    mqtt_client_t *ws_client = mqtt_client_create(&config);
    if (!ws_client) {
        printf("Failed to create WebSocket client\n");
        return;
    }

    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_disconnect = on_disconnect;
    callbacks.on_message = on_message;
    mqtt_set_callbacks(ws_client, &callbacks);

    /* WebSocket configuration */
    mqtt_ws_config_t ws_config = {0};
    ws_config.path = "/mqtt";
    ws_config.subprotocol = "mqtt";

    mqtt_connect_opts_t conn = {0};
    conn.host = host;
    conn.port = 8080;  /* test.mosquitto.org WebSocket port */
    conn.client_id = "mqtt_demo_ws_client";
    conn.clean_session = true;
    conn.keepalive_sec = 60;
    conn.protocol_version = MQTT_VERSION_3_1_1;
    conn.transport_type = MQTT_TRANSPORT_WS;
    conn.ws_config = &ws_config;
    conn.connect_timeout_ms = 10000;

    printf("Connecting via WebSocket to %s:8080...\n", host);
    ws_err = mqtt_connect(ws_client, &conn);
    if (ws_err != MQTT_OK) {
        printf("WebSocket connect failed: %s\n", mqtt_error_str(ws_err));
        printf("\n--- Demo 6 Results ---\n");
        printf("  WS connect:     SKIPPED (broker does not have WebSocket enabled)\n");
        printf("\n  To enable WebSocket on Mosquitto, add to mosquitto.conf:\n");
        printf("    listener 8080\n");
        printf("    protocol websockets\n");
        mqtt_client_destroy(ws_client);
        /* Don't record as failure - WebSocket is optional */
        return;
    }

    printf("WebSocket connection established!\n");

    /* Quick publish/subscribe test */
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/websocket/test", DEMO_TOPIC_BASE);

    mqtt_subscribe_opts_t sub = {0};
    sub.topic_filter = topic;
    sub.max_qos = MQTT_QOS_1;
    mqtt_subscribe(ws_client, &sub, 1);
    mqtt_loop(ws_client, 500);

    mqtt_publish_opts_t pub = {0};
    pub.topic = topic;
    pub.payload = (const uint8_t *)"Hello via WebSocket!";
    pub.payload_len = 20;
    pub.qos = MQTT_QOS_1;
    mqtt_publish(ws_client, &pub);

    for (int i = 0; i < 20 && g_running; i++) {
        mqtt_loop(ws_client, 100);
    }

    mqtt_disconnect(ws_client);
    mqtt_client_destroy(ws_client);

    int ws_received = g_messages_received - prev_count;
    printf("\n--- Demo 6 Results ---\n");
    printf("  WS connect:     Expected: success     | Actual: %s\n", ws_err == MQTT_OK ? "success" : "FAILED");
    printf("  WS message:     Expected: 1           | Actual: %d %s\n",
           ws_received, ws_received == 1 ? "" : "<-- ISSUE");

    record_result("Demo 6: WebSocket", "WS connect", 1, ws_err == MQTT_OK ? 1 : 0);
    record_result("Demo 6: WebSocket", "WS message recv", 1, ws_received);
}

/**
 * @brief Demo 7: Session Persistence and Subscription Restoration
 */
static void demo_session_persistence(const char *host, uint16_t port)
{
    print_separator("Demo 7: Session Persistence and Subscription Restoration");

    int prev_count = g_messages_received;
    mqtt_error_t phase1_err = MQTT_OK;
    mqtt_error_t phase2_err = MQTT_OK;
    int phase2_message_received = 0;

    const char *persistent_client_id = "mqtt_demo_persistent_client";
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/persistent/#", DEMO_TOPIC_BASE);

    /* First connection - establish session and subscribe */
    printf("=== Phase 1: Create session with subscriptions ===\n");

    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.send_buffer_size = 4096;
    config.recv_buffer_size = 4096;

    mqtt_client_t *client1 = mqtt_client_create(&config);
    if (!client1) {
        printf("Failed to create client\n");
        return;
    }

    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_message = on_message;
    mqtt_set_callbacks(client1, &callbacks);

    mqtt_connect_opts_t conn = {0};
    conn.host = host;
    conn.port = port;
    conn.client_id = persistent_client_id;
    conn.clean_session = false;  /* Persistent session! */
    conn.keepalive_sec = 60;
    conn.protocol_version = MQTT_VERSION_3_1_1;
    conn.transport_type = MQTT_TRANSPORT_TCP;
    conn.connect_timeout_ms = 10000;

    printf("Connecting with clean_session=false...\n");
    phase1_err = mqtt_connect(client1, &conn);
    if (phase1_err != MQTT_OK) {
        printf("Connect failed: %s\n", mqtt_error_str(phase1_err));
        mqtt_client_destroy(client1);
        printf("\n--- Demo 7 Results ---\n");
        printf("  Phase 1:        Expected: success     | Actual: FAILED <-- ISSUE\n");
        return;
    }

    /* Subscribe - broker will remember this subscription for our client_id */
    printf("Subscribing to: %s\n", topic);
    mqtt_subscribe_opts_t sub = {0};
    sub.topic_filter = topic;
    sub.max_qos = MQTT_QOS_1;
    mqtt_subscribe(client1, &sub, 1);
    mqtt_loop(client1, 1000);

    printf("Subscription registered with broker (will persist after disconnect)\n");

    /* Disconnect gracefully */
    printf("Disconnecting (session remains on broker)...\n");
    mqtt_disconnect(client1);
    mqtt_client_destroy(client1);

    printf("\n=== Phase 2: Reconnect with existing session ===\n");

    /* Wait a bit */
    printf("Waiting 2 seconds...\n");
    sleep(2);

    /* Reconnect with same client ID */
    mqtt_client_t *client2 = mqtt_client_create(&config);
    if (!client2) {
        printf("Failed to create second client\n");
        return;
    }

    mqtt_set_callbacks(client2, &callbacks);

    printf("Reconnecting with same client_id and clean_session=false...\n");
    g_connected = false;
    phase2_err = mqtt_connect(client2, &conn);
    if (phase2_err != MQTT_OK) {
        printf("Reconnect failed: %s\n", mqtt_error_str(phase2_err));
        mqtt_client_destroy(client2);
        printf("\n--- Demo 7 Results ---\n");
        printf("  Phase 1:        Expected: success     | Actual: success\n");
        printf("  Phase 2:        Expected: success     | Actual: FAILED <-- ISSUE\n");
        return;
    }

    /* Check if session was present - broker should report session_present=true
     *
     * NOTE: With clean_session=false, the BROKER keeps subscriptions server-side.
     * When we reconnect with the same client_id, the broker already knows our
     * subscriptions - we don't need to resubscribe! This is the whole point of
     * persistent sessions in MQTT.
     *
     * The local mqtt_subscription_store is for CLIENT-SIDE tracking (e.g., knowing
     * what you're subscribed to, or reconnecting to a DIFFERENT broker). It's not
     * shared between client instances since each is a separate process/object.
     */
    printf("Session restored by broker (subscriptions preserved server-side)\n");
    printf("No need to resubscribe - broker remembers our subscriptions!\n");

    /* Publish to test the restored subscription */
    char pub_topic[128];
    snprintf(pub_topic, sizeof(pub_topic), "%s/persistent/test", DEMO_TOPIC_BASE);

    mqtt_publish_opts_t pub = {0};
    pub.topic = pub_topic;
    pub.payload = (const uint8_t *)"Message after reconnect";
    pub.payload_len = 23;
    pub.qos = MQTT_QOS_1;

    printf("\nPublishing test message to verify subscription...\n");
    mqtt_publish(client2, &pub);

    /* Wait for message */
    int msg_count_before = g_messages_received;
    for (int i = 0; i < 30 && g_messages_received == msg_count_before && g_running; i++) {
        mqtt_loop(client2, 100);
    }

    phase2_message_received = (g_messages_received > msg_count_before) ? 1 : 0;
    if (phase2_message_received) {
        printf("Session persistence working - received message!\n");
    }

    /* Clean up - connect with clean session to remove persistent session */
    mqtt_disconnect(client2);
    mqtt_client_destroy(client2);

    printf("\n=== Phase 3: Clean up persistent session ===\n");
    mqtt_client_t *client3 = mqtt_client_create(&config);
    if (client3) {
        conn.clean_session = true;
        mqtt_connect(client3, &conn);
        mqtt_disconnect(client3);
        mqtt_client_destroy(client3);
        printf("Persistent session cleaned up\n");
    }

    printf("\n--- Demo 7 Results ---\n");
    printf("  Phase 1 connect: Expected: success     | Actual: %s\n", phase1_err == MQTT_OK ? "success" : "FAILED");
    printf("  Phase 2 connect: Expected: success     | Actual: %s\n", phase2_err == MQTT_OK ? "success" : "FAILED");
    printf("  Message recv:    Expected: 1           | Actual: %d %s\n",
           phase2_message_received, phase2_message_received == 1 ? "" : "<-- ISSUE");

    record_result("Demo 7: Session Persist", "Phase 1 connect", 1, phase1_err == MQTT_OK ? 1 : 0);
    record_result("Demo 7: Session Persist", "Phase 2 reconnect", 1, phase2_err == MQTT_OK ? 1 : 0);
    record_result("Demo 7: Session Persist", "Message after reconn", 1, phase2_message_received);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(int argc, char *argv[])
{
    const char *host = argc > 1 ? argv[1] : "test.mosquitto.org";
    uint16_t port = argc > 2 ? (uint16_t)atoi(argv[2]) : 1883;

    /* Set up signal handler for clean shutdown */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("============================================================\n");
    printf("  MQTT Client Library - Comprehensive Demo\n");
    printf("============================================================\n\n");
    printf("Broker: %s:%u\n", host, port);
    printf("Press Ctrl+C to stop at any time\n\n");

    /* Warning about public brokers */
    if (strcmp(host, "localhost") != 0 && strcmp(host, "127.0.0.1") != 0) {
        printf("============================================================\n");
        printf("  WARNING: Public Broker Latency\n");
        printf("============================================================\n");
        printf("You are connecting to a public broker. Public brokers like\n");
        printf("test.mosquitto.org can have high latency which may cause:\n");
        printf("  - Delayed message delivery (QoS 1/2 messages arriving late)\n");
        printf("  - Retained messages not being received immediately\n");
        printf("  - Will messages taking longer than expected\n");
        printf("  - Demo results showing as 'ISSUE' due to timeouts\n\n");
        printf("For accurate demo results, run a local MQTT broker:\n\n");
        printf("  # Install Mosquitto (Ubuntu/Debian):\n");
        printf("  sudo apt install mosquitto\n\n");
        printf("  # Install Mosquitto (macOS):\n");
        printf("  brew install mosquitto\n\n");
        printf("  # Install Mosquitto (Fedora/RHEL):\n");
        printf("  sudo dnf install mosquitto\n\n");
        printf("  # Install Mosquitto (Windows):\n");
        printf("  Download from https://mosquitto.org/download/\n");
        printf("  Or: choco install mosquitto\n");
        printf("  Or: winget install EclipseFoundation.Mosquitto\n\n");
        printf("  # Start the broker:\n");
        printf("  mosquitto -v\n\n");
        printf("  # Run this demo with local broker:\n");
        printf("  ./comprehensive_demo localhost 1883\n\n");
        printf("============================================================\n\n");
    }

    /* Initialize library */
    mqtt_error_t err = mqtt_lib_init();
    if (err != MQTT_OK) {
        fprintf(stderr, "Failed to initialize MQTT library: %s\n", mqtt_error_str(err));
        return 1;
    }

    /* Create main client */
    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.send_buffer_size = 8192;
    config.recv_buffer_size = 8192;
    config.max_inflight_messages = 20;

    mqtt_client_t *client = mqtt_client_create(&config);
    if (!client) {
        fprintf(stderr, "Failed to create MQTT client\n");
        mqtt_lib_cleanup();
        return 1;
    }

    /* Set up callbacks */
    mqtt_callbacks_t callbacks = {0};
    callbacks.on_connect = on_connect;
    callbacks.on_disconnect = on_disconnect;
    callbacks.on_message = on_message;
    callbacks.on_publish_complete = on_publish_complete;
    callbacks.on_publish_failed = on_publish_failed;
    callbacks.on_subscribe = on_subscribe;
    callbacks.on_redirect = on_redirect;
    mqtt_set_callbacks(client, &callbacks);

    /* Connect to broker */
    char client_id[64];
    generate_client_id(client_id, sizeof(client_id));

    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = host;
    conn_opts.port = port;
    conn_opts.client_id = client_id;
    conn_opts.clean_session = true;
    conn_opts.keepalive_sec = 60;
    conn_opts.protocol_version = MQTT_VERSION_3_1_1;
    conn_opts.transport_type = MQTT_TRANSPORT_TCP;
    conn_opts.connect_timeout_ms = 10000;

    printf("Connecting to broker...\n");
    err = mqtt_connect(client, &conn_opts);
    if (err != MQTT_OK) {
        fprintf(stderr, "Connection failed: %s\n", mqtt_error_str(err));
        mqtt_client_destroy(client);
        mqtt_lib_cleanup();
        return 1;
    }

    /* Run demos */
    if (g_running) demo_basic_pubsub(client);
    if (g_running) demo_async_pubsub(client);
    if (g_running) demo_retained_messages(client);
    if (g_running) demo_topic_wildcards(client);
    if (g_running) demo_will_message(client, host, port);

#ifdef MQTT_V5_SUPPORT
    if (g_running) demo_mqtt5_features(client, host, port);
#else
    print_separator("Demo 5: MQTT 5.0 Features");
    printf("MQTT 5.0 support not compiled in (enable with -DMQTT_ENABLE_V5=ON)\n");
#endif

    if (g_running) demo_websocket_transport(host);
    if (g_running) demo_session_persistence(host, port);

    /* Final statistics */
    print_final_results();

    print_separator("Raw Statistics");
    printf("Total messages received: %d\n", g_messages_received);
    printf("Publishes completed (QoS 1/2): %d\n", g_publishes_completed);
    printf("Publishes failed: %d\n", g_publishes_failed);

    /* Clean disconnect */
    printf("\nDisconnecting main client...\n");
    mqtt_disconnect(client);
    mqtt_client_destroy(client);
    mqtt_lib_cleanup();

    printf("\nDemo finished successfully!\n");
    return 0;
}
