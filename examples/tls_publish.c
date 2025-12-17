/*
 * TLS MQTT Publisher Example
 * Connects to a broker using TLS/SSL and publishes a message.
 *
 * Usage: ./tls_publish [broker_host] [port] [topic] [message]
 * Defaults: test.mosquitto.org 8883 test/topic "Hello Secure MQTT!"
 *
 * Note: test.mosquitto.org:8883 uses server-side TLS with a CA-signed cert.
 *       For self-signed certs, you'll need to provide a CA certificate.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "mqtt/mqtt.h"
#include "mqtt/mqtt_config.h"

#ifndef MQTT_ENABLE_TLS
int main(void) {
    fprintf(stderr, "TLS support is not enabled in this build.\n");
    fprintf(stderr, "Rebuild with -DMQTT_ENABLE_TLS=ON\n");
    return 1;
}
#else

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
    /* test.mosquitto.org provides TLS on port 8883 with a publicly trusted cert */
    const char *host = argc > 1 ? argv[1] : "test.mosquitto.org";
    uint16_t port = argc > 2 ? (uint16_t)atoi(argv[2]) : 8883;
    const char *topic = argc > 3 ? argv[3] : "mqtt_client_test/tls_hello";
    const char *message = argc > 4 ? argv[4] : "Hello Secure MQTT!";

    mqtt_error_t err;

    printf("TLS MQTT Publisher Example\n");
    printf("==========================\n\n");

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

    /* Set callbacks */
    mqtt_callbacks_t callbacks = {0};
    callbacks.on_publish_complete = on_publish_complete;
    mqtt_set_callbacks(client, &callbacks);

    /* Configure TLS */
    mqtt_tls_config_t tls_config = {0};
    tls_config.verify_peer = true;       /* Verify server certificate */
    tls_config.verify_hostname = true;   /* Verify hostname matches cert */
    /* Using system CA store (NULL ca_cert_path uses default paths) */
    /* For custom CA: tls_config.ca_cert_path = "/path/to/ca.pem"; */

    /* Connect to broker with TLS */
    mqtt_connect_opts_t conn_opts = {0};
    conn_opts.host = host;
    conn_opts.port = port;
    conn_opts.client_id = "mqtt_tls_example_publisher";
    conn_opts.clean_session = true;
    conn_opts.keepalive_sec = 60;
    conn_opts.protocol_version = MQTT_VERSION_3_1_1;
    conn_opts.transport_type = MQTT_TRANSPORT_TLS;
    conn_opts.tls_config = &tls_config;
    conn_opts.connect_timeout_ms = 15000;  /* TLS handshake can take longer */

    printf("Connecting to %s:%u (TLS)...\n", host, port);
    err = mqtt_connect(client, &conn_opts);
    if (err != MQTT_OK) {
        fprintf(stderr, "Connect failed: %s\n", mqtt_error_str(err));
        mqtt_client_destroy(client);
        mqtt_lib_cleanup();
        return 1;
    }
    printf("Connected with TLS!\n\n");

    /* Publish QoS 1 message */
    printf("Publishing to '%s': %s\n", topic, message);
    mqtt_publish_opts_t pub_opts = {0};
    pub_opts.topic = topic;
    pub_opts.payload = (const uint8_t *)message;
    pub_opts.payload_len = strlen(message);
    pub_opts.qos = MQTT_QOS_1;
    pub_opts.retain = false;

    publish_complete = false;
    err = mqtt_publish(client, &pub_opts);
    if (err != MQTT_OK) {
        fprintf(stderr, "Publish failed: %s\n", mqtt_error_str(err));
    } else {
        printf("  -> Waiting for PUBACK...\n");
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

#endif /* MQTT_ENABLE_TLS */
