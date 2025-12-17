/*
 * Simple MQTT Publisher Example
 * Connects to a broker, publishes messages at QoS 0, 1, and 2.
 *
 * Usage: ./simple_publish [broker_host] [port] [topic] [message]
 * Defaults: test.mosquitto.org 1883 test/topic "Hello MQTT!"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "mqtt/mqtt.h"

/* Track publish completions for QoS 1/2 */
static volatile bool publish_complete = false;
static volatile uint16_t last_packet_id = 0;

/* Callback for QoS 1/2 publish completion */
static void on_publish_complete(mqtt_client_t *client, void *user_data, uint16_t packet_id) {
    (void)client;
    (void)user_data;
    printf("  -> Publish acknowledged (packet_id=%u)\n", packet_id);
    last_packet_id = packet_id;
    publish_complete = true;
}

int main(int argc, char *argv[]) {
    const char *host = argc > 1 ? argv[1] : "test.mosquitto.org";
    uint16_t port = argc > 2 ? (uint16_t)atoi(argv[2]) : 1883;
    const char *topic = argc > 3 ? argv[3] : "mqtt_client_test/hello";
    const char *message = argc > 4 ? argv[4] : "Hello MQTT!";

    mqtt_error_t err;

    /* Initialize library */
    err = mqtt_lib_init();
    if (err != MQTT_OK) {
        fprintf(stderr, "Failed to init library: %s\n", mqtt_error_str(err));
        return 1;
    }

    /* Create client */
    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.send_buffer_size = 4096;
    config.recv_buffer_size = 4096;
    config.max_inflight_messages = 10;

    mqtt_client_t *client = mqtt_client_create(&config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        mqtt_lib_cleanup();
        return 1;
    }

    /* Set callbacks for QoS 1/2 publish acknowledgments */
    mqtt_callbacks_t callbacks = {0};
    callbacks.on_publish_complete = on_publish_complete;
    mqtt_set_callbacks(client, &callbacks);

    /* Connect to broker */
    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = host;
    conn_opts.port = port;
    conn_opts.client_id = "mqtt_example_publisher";
    conn_opts.clean_session = true;
    conn_opts.keepalive_sec = 60;
    conn_opts.protocol_version = MQTT_VERSION_3_1_1;
    conn_opts.transport_type = MQTT_TRANSPORT_TCP;
    conn_opts.connect_timeout_ms = 10000;

    printf("Connecting to %s:%u...\n", host, port);
    err = mqtt_connect(client, &conn_opts);
    if (err != MQTT_OK) {
        fprintf(stderr, "Connect failed: %s\n", mqtt_error_str(err));
        mqtt_client_destroy(client);
        mqtt_lib_cleanup();
        return 1;
    }
    printf("Connected!\n\n");

    /* Publish QoS 0 (fire and forget) */
    printf("=== QoS 0 (At most once) ===\n");
    mqtt_publish_opts_t pub_opts = {0};
    pub_opts.topic = topic;
    pub_opts.payload = (const uint8_t *)message;
    pub_opts.payload_len = strlen(message);
    pub_opts.qos = MQTT_QOS_0;
    pub_opts.retain = false;

    printf("Publishing to '%s': %s\n", topic, message);
    err = mqtt_publish(client, &pub_opts);
    if (err != MQTT_OK) {
        fprintf(stderr, "Publish failed: %s\n", mqtt_error_str(err));
    } else {
        printf("  -> Sent (no acknowledgment for QoS 0)\n");
    }

    /* Publish QoS 1 (at least once) */
    printf("\n=== QoS 1 (At least once) ===\n");
    pub_opts.qos = MQTT_QOS_1;
    publish_complete = false;

    printf("Publishing to '%s': %s\n", topic, message);
    err = mqtt_publish(client, &pub_opts);
    if (err != MQTT_OK) {
        fprintf(stderr, "Publish failed: %s\n", mqtt_error_str(err));
    } else {
        printf("  -> Waiting for PUBACK...\n");
        /* Wait for acknowledgment by calling mqtt_loop */
        int attempts = 0;
        while (!publish_complete && attempts < 50) {
            mqtt_loop(client, 100);  /* 100ms timeout per loop */
            attempts++;
        }
        if (!publish_complete) {
            fprintf(stderr, "  -> Timeout waiting for PUBACK\n");
        }
    }

    /* Publish QoS 2 (exactly once) */
    printf("\n=== QoS 2 (Exactly once) ===\n");
    pub_opts.qos = MQTT_QOS_2;
    publish_complete = false;

    printf("Publishing to '%s': %s\n", topic, message);
    err = mqtt_publish(client, &pub_opts);
    if (err != MQTT_OK) {
        fprintf(stderr, "Publish failed: %s\n", mqtt_error_str(err));
    } else {
        printf("  -> Waiting for PUBREC/PUBREL/PUBCOMP handshake...\n");
        /* Wait for full QoS 2 handshake */
        int attempts = 0;
        while (!publish_complete && attempts < 50) {
            mqtt_loop(client, 100);
            attempts++;
        }
        if (!publish_complete) {
            fprintf(stderr, "  -> Timeout waiting for QoS 2 completion\n");
        }
    }

    /* Disconnect */
    printf("\nDisconnecting...\n");
    mqtt_disconnect(client);

    /* Cleanup */
    mqtt_client_destroy(client);
    mqtt_lib_cleanup();

    printf("Done!\n");
    return 0;
}
