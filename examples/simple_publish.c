/*
 * Simple MQTT Publisher Example
 * Connects to a broker, publishes a message, and disconnects.
 *
 * Usage: ./simple_publish [broker_host] [port] [topic] [message]
 * Defaults: localhost 1883 test/topic "Hello MQTT!"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mqtt/mqtt.h"

int main(int argc, char *argv[]) {
    const char *host = argc > 1 ? argv[1] : "localhost";
    uint16_t port = argc > 2 ? (uint16_t)atoi(argv[2]) : 1883;
    const char *topic = argc > 3 ? argv[3] : "test/topic";
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

    mqtt_client_t *client = mqtt_client_create(&config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        mqtt_lib_cleanup();
        return 1;
    }

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
    printf("Connected!\n");

    /* Publish message */
    mqtt_publish_opts_t pub_opts = {0};
    pub_opts.topic = topic;
    pub_opts.payload = message;
    pub_opts.payload_len = strlen(message);
    pub_opts.qos = MQTT_QOS_0;
    pub_opts.retain = false;

    printf("Publishing to '%s': %s\n", topic, message);
    err = mqtt_publish(client, &pub_opts);
    if (err != MQTT_OK) {
        fprintf(stderr, "Publish failed: %s\n", mqtt_error_str(err));
    } else {
        printf("Published successfully!\n");
    }

    /* Disconnect */
    printf("Disconnecting...\n");
    mqtt_disconnect(client);

    /* Cleanup */
    mqtt_client_destroy(client);
    mqtt_lib_cleanup();

    return err == MQTT_OK ? 0 : 1;
}
