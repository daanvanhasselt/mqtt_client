/*
 * WebSocket MQTT Publisher Example
 * Connects to a broker using WebSocket transport and publishes a message.
 *
 * Usage: ./ws_publish [broker_host] [port] [topic] [message]
 * Defaults: test.mosquitto.org 8080 test/topic "Hello via WebSocket!"
 *
 * Note: This example requires the library to be built with MQTT_ENABLE_WEBSOCKET=ON
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "mqtt/mqtt.h"

/* Track publish completions for QoS 1/2 */
static volatile bool publish_complete = false;

/* Callback for QoS 1/2 publish completion */
static void on_publish_complete(mqtt_client_t *client, void *user_data, uint16_t packet_id) {
    (void)client;
    (void)user_data;
    printf("  -> Publish acknowledged (packet_id=%u)\n", packet_id);
    publish_complete = true;
}

int main(int argc, char *argv[]) {
    const char *host = argc > 1 ? argv[1] : "test.mosquitto.org";
    uint16_t port = argc > 2 ? (uint16_t)atoi(argv[2]) : 8080;  /* WebSocket port */
    const char *topic = argc > 3 ? argv[3] : "mqtt_client_test/websocket";
    const char *message = argc > 4 ? argv[4] : "Hello via WebSocket!";

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

    /* Set callbacks for publish acknowledgments */
    mqtt_callbacks_t callbacks = {0};
    callbacks.on_publish_complete = on_publish_complete;
    mqtt_set_callbacks(client, &callbacks);

    /* Configure WebSocket transport */
    mqtt_ws_config_t ws_config = {0};
    ws_config.path = "/mqtt";            /* Standard MQTT WebSocket path */
    ws_config.subprotocol = "mqtt";      /* MQTT subprotocol */
    ws_config.extra_headers = NULL;      /* No extra headers */

    /* Connect to broker via WebSocket */
    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = host;
    conn_opts.port = port;
    conn_opts.client_id = "mqtt_ws_publisher";
    conn_opts.clean_session = true;
    conn_opts.keepalive_sec = 60;
    conn_opts.protocol_version = MQTT_VERSION_3_1_1;
    conn_opts.transport_type = MQTT_TRANSPORT_WS;  /* Use WebSocket transport */
    conn_opts.ws_config = &ws_config;
    conn_opts.connect_timeout_ms = 10000;

    printf("Connecting to %s:%u via WebSocket (path: %s)...\n",
           host, port, ws_config.path);

    err = mqtt_connect(client, &conn_opts);
    if (err != MQTT_OK) {
        fprintf(stderr, "Connect failed: %s\n", mqtt_error_str(err));
        fprintf(stderr, "\nNote: WebSocket support must be enabled at compile time.\n");
        fprintf(stderr, "Build with: cmake .. -DMQTT_ENABLE_WEBSOCKET=ON\n");
        mqtt_client_destroy(client);
        mqtt_lib_cleanup();
        return 1;
    }
    printf("Connected via WebSocket!\n\n");

    /* Publish QoS 1 message */
    printf("Publishing to '%s': %s\n", topic, message);
    mqtt_publish_opts_t pub_opts = {0};
    pub_opts.topic = topic;
    pub_opts.payload = (const uint8_t *)message;
    pub_opts.payload_len = strlen(message);
    pub_opts.qos = MQTT_QOS_1;
    pub_opts.retain = false;

    err = mqtt_publish(client, &pub_opts);
    if (err != MQTT_OK) {
        fprintf(stderr, "Publish failed: %s\n", mqtt_error_str(err));
    } else {
        printf("  -> Waiting for PUBACK...\n");
        /* Wait for acknowledgment */
        int attempts = 0;
        while (!publish_complete && attempts < 50) {
            mqtt_loop(client, 100);
            attempts++;
        }
        if (!publish_complete) {
            fprintf(stderr, "  -> Timeout waiting for PUBACK\n");
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
