/**
 * @file mqtt_client.c
 * @brief MQTT Client Core Implementation
 *
 * This file implements the core MQTT client functionality including:
 * - Library initialization/cleanup
 * - Client lifecycle management
 * - Synchronous connect/disconnect/publish/subscribe operations
 * - Event loop for message processing
 */

#include "mqtt_client_internal.h"
#include "../protocol/mqtt_v311/mqtt_v311.h"
#include "../core/mqtt_packet.h"
#include "../memory/mqtt_memory.h"
#include <string.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>

/* Library version */
#define MQTT_VERSION_MAJOR 1
#define MQTT_VERSION_MINOR 0
#define MQTT_VERSION_PATCH 0

/* Default configuration values */
#define DEFAULT_SEND_BUFFER_SIZE   8192
#define DEFAULT_RECV_BUFFER_SIZE   8192
#define DEFAULT_MAX_INFLIGHT       20
#define DEFAULT_MAX_PACKET_SIZE    (256 * 1024 * 1024)
#define DEFAULT_CONNECT_TIMEOUT    30000
#define DEFAULT_KEEPALIVE          60

/* Global library initialization state */
static int g_lib_initialized = 0;

/* ========================================================================== */
/* Library Lifecycle Functions                                                */
/* ========================================================================== */

mqtt_error_t mqtt_lib_init(void)
{
    if (g_lib_initialized) {
        return MQTT_OK;  /* Already initialized */
    }

    /* Initialize platform-specific components (if needed) */
    /* For now, this is a no-op as POSIX doesn't need global init */

    g_lib_initialized = 1;
    return MQTT_OK;
}

void mqtt_lib_cleanup(void)
{
    if (!g_lib_initialized) {
        return;
    }

    /* Cleanup platform-specific components (if needed) */
    g_lib_initialized = 0;
}

const char *mqtt_lib_version(void)
{
    static char version_str[32];
    snprintf(version_str, sizeof(version_str), "%d.%d.%d",
             MQTT_VERSION_MAJOR, MQTT_VERSION_MINOR, MQTT_VERSION_PATCH);
    return version_str;
}

/* ========================================================================== */
/* Internal Helper Functions                                                  */
/* ========================================================================== */

uint16_t mqtt_client_next_packet_id(mqtt_client_t *client)
{
    if (!client) {
        return 0;
    }

    /* Packet IDs are 1-65535 (0 is reserved) */
    client->next_packet_id++;
    if (client->next_packet_id == 0) {
        client->next_packet_id = 1;
    }

    return client->next_packet_id;
}

void mqtt_client_update_last_send(mqtt_client_t *client)
{
    if (client) {
        client->last_send_time = mqtt_time_monotonic_ms();
    }
}

void mqtt_client_update_last_recv(mqtt_client_t *client)
{
    if (client) {
        client->last_recv_time = mqtt_time_monotonic_ms();
        client->ping_outstanding = false;
    }
}

mqtt_error_t mqtt_client_send_packet(mqtt_client_t *client)
{
    if (!client || !client->transport) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (mqtt_buffer_empty(&client->send_buf)) {
        return MQTT_OK;  /* Nothing to send */
    }

    /* Send all data from buffer */
    const uint8_t *data = mqtt_buffer_data_const(&client->send_buf);
    size_t len = mqtt_buffer_len(&client->send_buf);
    size_t total_sent = 0;

    while (total_sent < len) {
        ssize_t sent = mqtt_transport_send(client->transport,
                                          data + total_sent,
                                          len - total_sent);
        if (sent < 0) {
            client->last_error = (mqtt_error_t)sent;
            return (mqtt_error_t)sent;
        }

        total_sent += sent;
    }

    /* Clear send buffer after successful send */
    mqtt_buffer_reset(&client->send_buf);
    mqtt_client_update_last_send(client);

    return MQTT_OK;
}

mqtt_error_t mqtt_client_recv_packet(mqtt_client_t *client, int timeout_ms)
{
    if (!client || !client->transport) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Wait for data with timeout using select */
    if (timeout_ms > 0) {
        int fd = mqtt_transport_get_fd(client->transport);
        if (fd < 0) {
            return MQTT_ERR_INVALID_STATE;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int ret = select(fd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            return MQTT_ERR_SOCKET;
        } else if (ret == 0) {
            return MQTT_ERR_TIMEOUT;
        }
    }

    /* Read data into receive buffer */
    uint8_t *write_ptr = mqtt_buffer_write_ptr(&client->recv_buf);
    size_t available = mqtt_buffer_write_available(&client->recv_buf);

    if (available == 0) {
        /* Buffer full, need to expand */
        mqtt_error_t err = mqtt_buffer_reserve(&client->recv_buf,
                                               client->recv_buf.capacity * 2);
        if (err != MQTT_OK) {
            return err;
        }
        write_ptr = mqtt_buffer_write_ptr(&client->recv_buf);
        available = mqtt_buffer_write_available(&client->recv_buf);
    }

    ssize_t received = mqtt_transport_recv(client->transport, write_ptr, available);
    if (received < 0) {
        client->last_error = (mqtt_error_t)received;
        return (mqtt_error_t)received;
    } else if (received == 0) {
        /* Connection closed */
        return MQTT_ERR_CONNECTION_LOST;
    }

    mqtt_buffer_advance_write(&client->recv_buf, received);
    mqtt_client_update_last_recv(client);

    return MQTT_OK;
}

/* ========================================================================== */
/* Client Lifecycle Functions                                                 */
/* ========================================================================== */

mqtt_client_t *mqtt_client_create(const mqtt_client_config_t *config)
{
    if (!g_lib_initialized) {
        return NULL;
    }

    mqtt_client_t *client = mqtt_malloc(sizeof(mqtt_client_t));
    if (!client) {
        return NULL;
    }

    memset(client, 0, sizeof(mqtt_client_t));

    /* Set default configuration */
    if (config) {
        client->config = *config;
    } else {
        client->config.protocol_version = MQTT_VERSION_3_1_1;
        client->config.transport_type = MQTT_TRANSPORT_TCP;
        client->config.send_buffer_size = DEFAULT_SEND_BUFFER_SIZE;
        client->config.recv_buffer_size = DEFAULT_RECV_BUFFER_SIZE;
        client->config.max_inflight_messages = DEFAULT_MAX_INFLIGHT;
        client->config.max_packet_size = DEFAULT_MAX_PACKET_SIZE;
    }

    /* Initialize buffers */
    mqtt_error_t err = mqtt_buffer_init(&client->send_buf,
                                       client->config.send_buffer_size);
    if (err != MQTT_OK) {
        mqtt_free(client);
        return NULL;
    }

    err = mqtt_buffer_init(&client->recv_buf,
                          client->config.recv_buffer_size);
    if (err != MQTT_OK) {
        mqtt_buffer_cleanup(&client->send_buf);
        mqtt_free(client);
        return NULL;
    }

    client->state = MQTT_STATE_DISCONNECTED;
    client->next_packet_id = 1;

#ifdef MQTT_THREAD_SAFE
    err = mqtt_mutex_init(&client->lock);
    if (err != MQTT_OK) {
        mqtt_buffer_cleanup(&client->send_buf);
        mqtt_buffer_cleanup(&client->recv_buf);
        mqtt_free(client);
        return NULL;
    }
#endif

    return client;
}

void mqtt_client_destroy(mqtt_client_t *client)
{
    if (!client) {
        return;
    }

    /* Disconnect if connected */
    if (client->state != MQTT_STATE_DISCONNECTED) {
        mqtt_disconnect(client);
    }

    /* Destroy transport */
    if (client->transport) {
        mqtt_transport_destroy(client->transport);
        client->transport = NULL;
    }

    /* Cleanup buffers */
    mqtt_buffer_cleanup(&client->send_buf);
    mqtt_buffer_cleanup(&client->recv_buf);

#ifdef MQTT_THREAD_SAFE
    mqtt_mutex_destroy(&client->lock);
#endif

    mqtt_free(client);
}

mqtt_error_t mqtt_client_get_error(mqtt_client_t *client)
{
    if (!client) {
        return MQTT_ERR_INVALID_ARG;
    }

    return client->last_error;
}

/* ========================================================================== */
/* Synchronous Connection Functions                                           */
/* ========================================================================== */

mqtt_error_t mqtt_connect(mqtt_client_t *client, const mqtt_connect_opts_t *opts)
{
    if (!client || !opts) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (client->state != MQTT_STATE_DISCONNECTED) {
        return MQTT_ERR_ALREADY_CONNECTED;
    }

    mqtt_error_t err;

    /* Create transport */
    client->transport = mqtt_transport_create(opts->transport_type,
                                             opts->tls_config,
                                             opts->ws_config);
    if (!client->transport) {
        client->last_error = MQTT_ERR_NOMEM;
        return MQTT_ERR_NOMEM;
    }

    client->state = MQTT_STATE_CONNECTING;

    /* Connect transport */
    uint32_t timeout = opts->connect_timeout_ms ? opts->connect_timeout_ms : DEFAULT_CONNECT_TIMEOUT;
    err = mqtt_transport_connect(client->transport, opts->host, opts->port, timeout);
    if (err != MQTT_OK) {
        client->last_error = err;
        client->state = MQTT_STATE_ERROR;
        return err;
    }

    client->state = MQTT_STATE_CONNECTED_TCP;

    /* Build CONNECT packet */
    mqtt_buffer_reset(&client->send_buf);
    ssize_t encoded = mqtt_v311_encode_connect(&client->send_buf, opts);
    if (encoded < 0) {
        client->last_error = (mqtt_error_t)encoded;
        client->state = MQTT_STATE_ERROR;
        return (mqtt_error_t)encoded;
    }

    /* Send CONNECT packet */
    client->state = MQTT_STATE_CONNECTING_MQTT;
    err = mqtt_client_send_packet(client);
    if (err != MQTT_OK) {
        client->last_error = err;
        client->state = MQTT_STATE_ERROR;
        return err;
    }

    /* Wait for CONNACK */
    err = mqtt_client_recv_packet(client, timeout);
    if (err != MQTT_OK) {
        client->last_error = err;
        client->state = MQTT_STATE_ERROR;
        return err;
    }

    /* Parse CONNACK */
    const uint8_t *data = mqtt_buffer_data_const(&client->recv_buf);
    size_t len = mqtt_buffer_len(&client->recv_buf);

    if (len < 2) {
        client->last_error = MQTT_ERR_MALFORMED_PACKET;
        client->state = MQTT_STATE_ERROR;
        return MQTT_ERR_MALFORMED_PACKET;
    }

    /* Check packet type */
    uint8_t packet_type = (data[0] >> 4) & 0x0F;
    if (packet_type != MQTT_PACKET_CONNACK) {
        client->last_error = MQTT_ERR_PROTOCOL;
        client->state = MQTT_STATE_ERROR;
        return MQTT_ERR_PROTOCOL;
    }

    /* Decode remaining length */
    uint32_t remaining_len = data[1];  /* For CONNACK, always 2 */
    if (remaining_len != 2 || len < 4) {
        client->last_error = MQTT_ERR_MALFORMED_PACKET;
        client->state = MQTT_STATE_ERROR;
        return MQTT_ERR_MALFORMED_PACKET;
    }

    mqtt_v311_connack_t connack;
    err = mqtt_v311_decode_connack(data + 2, 2, &connack);
    if (err != MQTT_OK) {
        client->last_error = err;
        client->state = MQTT_STATE_ERROR;
        return err;
    }

    /* Check return code */
    if (connack.return_code != 0) {
        /* Map CONNACK return code to error */
        switch (connack.return_code) {
            case 1: err = MQTT_ERR_V311_UNACCEPTABLE_PROTOCOL; break;
            case 2: err = MQTT_ERR_V311_IDENTIFIER_REJECTED; break;
            case 3: err = MQTT_ERR_V311_SERVER_UNAVAILABLE; break;
            case 4: err = MQTT_ERR_V311_BAD_CREDENTIALS; break;
            case 5: err = MQTT_ERR_V311_NOT_AUTHORIZED; break;
            default: err = MQTT_ERR_PROTOCOL; break;
        }
        client->last_error = err;
        client->state = MQTT_STATE_ERROR;
        return err;
    }

    /* Successfully connected */
    client->state = MQTT_STATE_CONNECTED;
    client->protocol_version = opts->protocol_version;
    client->clean_session = opts->clean_session;
    client->keepalive_sec = opts->keepalive_sec ? opts->keepalive_sec : DEFAULT_KEEPALIVE;
    client->last_send_time = mqtt_time_monotonic_ms();
    client->last_recv_time = mqtt_time_monotonic_ms();
    client->ping_outstanding = false;

    /* Clear receive buffer */
    mqtt_buffer_reset(&client->recv_buf);
    client->recv_offset = 0;

    /* Invoke on_connect callback */
    if (client->callbacks.on_connect) {
        client->callbacks.on_connect(client, client->callbacks.user_data,
                                    connack.session_present);
    }

    return MQTT_OK;
}

mqtt_error_t mqtt_disconnect(mqtt_client_t *client)
{
    if (!client) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (client->state != MQTT_STATE_CONNECTED) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    mqtt_error_t err;

    /* Build DISCONNECT packet */
    mqtt_buffer_reset(&client->send_buf);
    ssize_t encoded = mqtt_v311_encode_disconnect(&client->send_buf);
    if (encoded < 0) {
        client->last_error = (mqtt_error_t)encoded;
        return (mqtt_error_t)encoded;
    }

    /* Send DISCONNECT packet */
    client->state = MQTT_STATE_DISCONNECTING;
    err = mqtt_client_send_packet(client);
    if (err != MQTT_OK) {
        /* Non-fatal, continue with disconnect */
    }

    /* Close transport */
    if (client->transport) {
        mqtt_transport_disconnect(client->transport);
    }

    client->state = MQTT_STATE_DISCONNECTED;

    /* Invoke on_disconnect callback */
    if (client->callbacks.on_disconnect) {
        client->callbacks.on_disconnect(client, client->callbacks.user_data, 0);
    }

    return MQTT_OK;
}

/* ========================================================================== */
/* Synchronous Publish Function                                               */
/* ========================================================================== */

mqtt_error_t mqtt_publish(mqtt_client_t *client, const mqtt_publish_opts_t *opts)
{
    if (!client || !opts) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (client->state != MQTT_STATE_CONNECTED) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    mqtt_error_t err;
    uint16_t packet_id = 0;

    /* For QoS 0, packet_id is 0. For QoS 1/2, allocate packet ID */
    if (opts->qos > MQTT_QOS_0) {
        packet_id = mqtt_client_next_packet_id(client);
    }

    /* Build PUBLISH packet */
    mqtt_buffer_reset(&client->send_buf);
    ssize_t encoded = mqtt_v311_encode_publish(&client->send_buf, opts, packet_id);
    if (encoded < 0) {
        client->last_error = (mqtt_error_t)encoded;
        return (mqtt_error_t)encoded;
    }

    /* Send PUBLISH packet */
    err = mqtt_client_send_packet(client);
    if (err != MQTT_OK) {
        client->last_error = err;
        return err;
    }

    /* For QoS 0, we're done */
    if (opts->qos == MQTT_QOS_0) {
        return MQTT_OK;
    }

    /* For QoS 1/2, wait for acknowledgment (simplified for now) */
    /* TODO: Implement proper QoS 1/2 handling with inflight tracking */

    return MQTT_OK;
}

/* ========================================================================== */
/* Synchronous Subscribe Function                                             */
/* ========================================================================== */

mqtt_error_t mqtt_subscribe(mqtt_client_t *client, const mqtt_subscribe_opts_t *opts, size_t count)
{
    if (!client || !opts || count == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (client->state != MQTT_STATE_CONNECTED) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    mqtt_error_t err;

    /* Allocate packet ID */
    uint16_t packet_id = mqtt_client_next_packet_id(client);

    /* Convert mqtt_subscribe_opts_t to mqtt_v311_subscription_t */
    mqtt_v311_subscription_t *subs = mqtt_malloc(sizeof(mqtt_v311_subscription_t) * count);
    if (!subs) {
        return MQTT_ERR_NOMEM;
    }

    for (size_t i = 0; i < count; i++) {
        subs[i].topic_filter = opts[i].topic_filter;
        subs[i].qos = opts[i].max_qos;
    }

    /* Build SUBSCRIBE packet */
    mqtt_buffer_reset(&client->send_buf);
    ssize_t encoded = mqtt_v311_encode_subscribe(&client->send_buf, packet_id, subs, count);
    mqtt_free(subs);

    if (encoded < 0) {
        client->last_error = (mqtt_error_t)encoded;
        return (mqtt_error_t)encoded;
    }

    /* Send SUBSCRIBE packet */
    err = mqtt_client_send_packet(client);
    if (err != MQTT_OK) {
        client->last_error = err;
        return err;
    }

    /* Wait for SUBACK */
    err = mqtt_client_recv_packet(client, 5000);  /* 5 second timeout */
    if (err != MQTT_OK) {
        client->last_error = err;
        return err;
    }

    /* Parse SUBACK */
    const uint8_t *data = mqtt_buffer_data_const(&client->recv_buf);
    size_t len = mqtt_buffer_len(&client->recv_buf);

    if (len < 2) {
        client->last_error = MQTT_ERR_MALFORMED_PACKET;
        return MQTT_ERR_MALFORMED_PACKET;
    }

    uint8_t packet_type = (data[0] >> 4) & 0x0F;
    if (packet_type != MQTT_PACKET_SUBACK) {
        client->last_error = MQTT_ERR_PROTOCOL;
        return MQTT_ERR_PROTOCOL;
    }

    /* Skip fixed header parsing for now, just verify we got SUBACK */
    mqtt_buffer_reset(&client->recv_buf);

    return MQTT_OK;
}

/* ========================================================================== */
/* Event Loop Functions                                                       */
/* ========================================================================== */

mqtt_error_t mqtt_loop(mqtt_client_t *client, int timeout_ms)
{
    if (!client) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (client->state != MQTT_STATE_CONNECTED) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    mqtt_error_t err;

    /* Check if we need to send PINGREQ */
    uint64_t now = mqtt_time_monotonic_ms();
    uint64_t keepalive_ms = client->keepalive_sec * 1000;

    if (keepalive_ms > 0 && (now - client->last_send_time) >= keepalive_ms) {
        if (client->ping_outstanding) {
            /* PINGRESP not received, connection lost */
            client->state = MQTT_STATE_ERROR;
            client->last_error = MQTT_ERR_CONNECTION_LOST;
            return MQTT_ERR_CONNECTION_LOST;
        }

        /* Send PINGREQ */
        mqtt_buffer_reset(&client->send_buf);
        ssize_t encoded = mqtt_v311_encode_pingreq(&client->send_buf);
        if (encoded < 0) {
            return (mqtt_error_t)encoded;
        }

        err = mqtt_client_send_packet(client);
        if (err != MQTT_OK) {
            return err;
        }

        client->ping_outstanding = true;
    }

    /* Try to receive data */
    err = mqtt_client_recv_packet(client, timeout_ms);
    if (err == MQTT_ERR_TIMEOUT) {
        return MQTT_OK;  /* Timeout is not an error in loop */
    } else if (err != MQTT_OK) {
        return err;
    }

    /* Process received packet */
    const uint8_t *data = mqtt_buffer_data_const(&client->recv_buf);
    size_t len = mqtt_buffer_len(&client->recv_buf);

    if (len < 2) {
        return MQTT_OK;  /* Not enough data yet */
    }

    uint8_t packet_type = (data[0] >> 4) & 0x0F;

    switch (packet_type) {
        case MQTT_PACKET_PUBLISH:
            /* TODO: Parse and dispatch PUBLISH message */
            /* For now, just consume the packet */
            mqtt_buffer_reset(&client->recv_buf);
            break;

        case MQTT_PACKET_PINGRESP:
            client->ping_outstanding = false;
            mqtt_buffer_reset(&client->recv_buf);
            break;

        default:
            /* Unknown or unexpected packet */
            mqtt_buffer_reset(&client->recv_buf);
            break;
    }

    return MQTT_OK;
}

mqtt_error_t mqtt_loop_tick(mqtt_client_t *client)
{
    return mqtt_loop(client, 0);
}

/* ========================================================================== */
/* Callback Management                                                        */
/* ========================================================================== */

void mqtt_set_callbacks(mqtt_client_t *client, const mqtt_callbacks_t *callbacks)
{
    if (!client || !callbacks) {
        return;
    }

    client->callbacks = *callbacks;
}
