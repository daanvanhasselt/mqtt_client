/*
 * Simple MQTT Subscriber Example
 * Connects to a broker, subscribes to a topic, and prints messages.
 *
 * Usage: ./simple_subscribe [broker_host] [port] [topic]
 * Defaults: localhost 1883 test/#
 * Press Ctrl+C to exit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "mqtt/mqtt.h"

static volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

void on_message(mqtt_client_t *client, void *user_data, const mqtt_message_t *msg) {
    (void)client;
    (void)user_data;

    printf("Received message on '%.*s': %.*s\n",
           (int)msg->topic_len, msg->topic,
           (int)msg->payload_len, (const char *)msg->payload);
}

void on_disconnect(mqtt_client_t *client, void *user_data, int reason_code) {
    (void)client;
    (void)user_data;
    printf("Disconnected with reason code: %d\n", reason_code);
    running = 0;
}

int main(int argc, char *argv[]) {
    const char *host = argc > 1 ? argv[1] : "localhost";
    uint16_t port = argc > 2 ? (uint16_t)atoi(argv[2]) : 1883;
    const char *topic = argc > 3 ? argv[3] : "mqtt_client_test/#";

    mqtt_error_t err;

    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

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

    mqtt_client_t *client = mqtt_client_create(&config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        mqtt_lib_cleanup();
        return 1;
    }

    /* Set callbacks */
    mqtt_callbacks_t callbacks = {0};
    callbacks.on_message = on_message;
    callbacks.on_disconnect = on_disconnect;
    mqtt_set_callbacks(client, &callbacks);

    /* Connect to broker */
    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = host;
    conn_opts.port = port;
    conn_opts.client_id = "mqtt_example_subscriber";
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
    printf("Connected!\n");

    /* Subscribe to topic */
    mqtt_subscribe_opts_t sub_opts = {0};
    sub_opts.topic_filter = topic;
    sub_opts.max_qos = MQTT_QOS_0;

    printf("Subscribing to '%s'...\n", topic);
    err = mqtt_subscribe(client, &sub_opts, 1);
    if (err != MQTT_OK) {
        fprintf(stderr, "Subscribe failed: %s\n", mqtt_error_str(err));
        mqtt_disconnect(client);
        mqtt_client_destroy(client);
        mqtt_lib_cleanup();
        return 1;
    }
    printf("Subscribed! Waiting for messages (Ctrl+C to quit)...\n");

    /* Main loop */
    while (running) {
        err = mqtt_loop(client, 1000);  /* 1 second timeout */
        if (err != MQTT_OK && err != MQTT_ERR_TIMEOUT) {
            fprintf(stderr, "Loop error: %s\n", mqtt_error_str(err));
            break;
        }
    }

    /* Disconnect */
    printf("\nDisconnecting...\n");
    mqtt_disconnect(client);

    /* Cleanup */
    mqtt_client_destroy(client);
    mqtt_lib_cleanup();

    return 0;
}
