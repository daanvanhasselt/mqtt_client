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
#include "../core/mqtt_varint.h"
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

mqtt_error_t mqtt_client_alloc_packet_id(mqtt_client_t *client, uint16_t *packet_id)
{
    if (!client || !packet_id) {
        return MQTT_ERR_INVALID_ARG;
    }

    return mqtt_packet_id_alloc(&client->packet_ids, packet_id);
}

void mqtt_client_free_packet_id(mqtt_client_t *client, uint16_t packet_id)
{
    if (!client || packet_id == 0) {
        return;
    }

    mqtt_packet_id_free(&client->packet_ids, packet_id);
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

    /* Initialize packet ID allocator */
    err = mqtt_packet_id_init(&client->packet_ids);
    if (err != MQTT_OK) {
        mqtt_buffer_cleanup(&client->send_buf);
        mqtt_buffer_cleanup(&client->recv_buf);
        mqtt_free(client);
        return NULL;
    }

    /* Initialize inflight queue */
    err = mqtt_inflight_init(&client->inflight, client->config.max_inflight_messages);
    if (err != MQTT_OK) {
        mqtt_buffer_cleanup(&client->send_buf);
        mqtt_buffer_cleanup(&client->recv_buf);
        mqtt_free(client);
        return NULL;
    }

    /* Initialize QoS 2 receive tracker */
    mqtt_qos2_recv_init(&client->qos2_recv, client->config.max_inflight_messages);

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

    /* Cleanup inflight queue */
    mqtt_inflight_cleanup(&client->inflight);

    /* Cleanup QoS 2 receive tracker */
    mqtt_qos2_recv_cleanup(&client->qos2_recv);

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

    /* Handle session state based on clean_session and session_present */
    if (opts->clean_session) {
        /* Clean session requested - clear any previous inflight state */
        mqtt_inflight_clear(&client->inflight);
        mqtt_packet_id_reset(&client->packet_ids);
        mqtt_qos2_recv_clear(&client->qos2_recv);
    } else if (!connack.session_present) {
        /* Server didn't have a session for us - clear our state too */
        mqtt_inflight_clear(&client->inflight);
        mqtt_packet_id_reset(&client->packet_ids);
        mqtt_qos2_recv_clear(&client->qos2_recv);
    }
    /* If clean_session=false AND session_present=true, we keep our inflight state
     * and can retry any pending QoS 1/2 messages on next loop iteration */

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

    /* If clean_session was true, clear inflight state */
    if (client->clean_session) {
        mqtt_inflight_clear(&client->inflight);
        mqtt_packet_id_reset(&client->packet_ids);
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
    if (!client || !opts || !opts->topic) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (client->state != MQTT_STATE_CONNECTED) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    mqtt_error_t err;
    uint16_t packet_id = 0;

    /* For QoS 0, packet_id is 0. For QoS 1/2, allocate packet ID */
    if (opts->qos > MQTT_QOS_0) {
        /* Check inflight queue capacity */
        if (mqtt_inflight_is_full(&client->inflight)) {
            return MQTT_ERR_INFLIGHT_FULL;
        }

        err = mqtt_client_alloc_packet_id(client, &packet_id);
        if (err != MQTT_OK) {
            client->last_error = err;
            return err;
        }
    }

    /* Build PUBLISH packet */
    mqtt_buffer_reset(&client->send_buf);
    ssize_t encoded = mqtt_v311_encode_publish(&client->send_buf, opts, packet_id);
    if (encoded < 0) {
        if (packet_id != 0) {
            mqtt_client_free_packet_id(client, packet_id);
        }
        client->last_error = (mqtt_error_t)encoded;
        return (mqtt_error_t)encoded;
    }

    /* Send PUBLISH packet */
    err = mqtt_client_send_packet(client);
    if (err != MQTT_OK) {
        if (packet_id != 0) {
            mqtt_client_free_packet_id(client, packet_id);
        }
        client->last_error = err;
        return err;
    }

    /* For QoS 0, we're done */
    if (opts->qos == MQTT_QOS_0) {
        return MQTT_OK;
    }

    /* For QoS 1/2, add to inflight queue */
    uint64_t now = mqtt_time_monotonic_ms();
    err = mqtt_inflight_add(&client->inflight,
                            packet_id,
                            opts->qos,
                            opts->topic,
                            opts->payload,
                            opts->payload_len,
                            opts->retain,
                            now);
    if (err != MQTT_OK) {
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = err;
        return err;
    }

    /* For synchronous API, we could wait for ACK here, but for async compatibility
     * we return immediately. The ACK will be handled in mqtt_loop().
     * Users wanting synchronous behavior should call mqtt_loop() until
     * their packet_id is acknowledged via the on_publish_complete callback. */

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
    uint16_t packet_id;
    err = mqtt_client_alloc_packet_id(client, &packet_id);
    if (err != MQTT_OK) {
        client->last_error = err;
        return err;
    }

    /* Convert mqtt_subscribe_opts_t to mqtt_v311_subscription_t */
    mqtt_v311_subscription_t *subs = mqtt_malloc(sizeof(mqtt_v311_subscription_t) * count);
    if (!subs) {
        mqtt_client_free_packet_id(client, packet_id);
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
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = MQTT_ERR_PROTOCOL;
        return MQTT_ERR_PROTOCOL;
    }

    /* Decode remaining length to find variable header start */
    uint32_t remaining_len;
    int varint_bytes = mqtt_varint_decode(data + 1, len - 1, &remaining_len);
    if (varint_bytes < 0) {
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = MQTT_ERR_MALFORMED_PACKET;
        return MQTT_ERR_MALFORMED_PACKET;
    }

    size_t header_len = 1 + varint_bytes;
    if (len < header_len + remaining_len) {
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = MQTT_ERR_MALFORMED_PACKET;
        return MQTT_ERR_MALFORMED_PACKET;
    }

    /* Parse SUBACK using the v311 decoder */
    mqtt_v311_suback_t suback = {0};
    err = mqtt_v311_decode_suback(data + header_len, remaining_len, &suback);
    if (err != MQTT_OK) {
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = err;
        return err;
    }

    /* Verify packet ID matches */
    if (suback.packet_id != packet_id) {
        mqtt_free(suback.return_codes);
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = MQTT_ERR_PROTOCOL;
        return MQTT_ERR_PROTOCOL;
    }

    /* Verify we got the right number of return codes */
    if (suback.count != count) {
        mqtt_free(suback.return_codes);
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = MQTT_ERR_PROTOCOL;
        return MQTT_ERR_PROTOCOL;
    }

    /* Check for subscription failures (0x80) and convert to QoS array */
    mqtt_qos_t *granted_qos = NULL;
    bool has_failure = false;

    if (client->callbacks.on_subscribe) {
        granted_qos = mqtt_malloc(sizeof(mqtt_qos_t) * count);
        if (granted_qos) {
            for (size_t i = 0; i < count; i++) {
                if (suback.return_codes[i] == 0x80) {
                    /* Subscription failed - set to an invalid QoS value */
                    granted_qos[i] = (mqtt_qos_t)0x80;
                    has_failure = true;
                } else {
                    granted_qos[i] = (mqtt_qos_t)suback.return_codes[i];
                }
            }
            /* Invoke callback with granted QoS levels */
            client->callbacks.on_subscribe(client, client->callbacks.user_data,
                                           packet_id, granted_qos, count);
            mqtt_free(granted_qos);
        }
    } else {
        /* Check for failures even without callback */
        for (size_t i = 0; i < count; i++) {
            if (suback.return_codes[i] == 0x80) {
                has_failure = true;
                break;
            }
        }
    }

    /* Free the return codes array */
    mqtt_free(suback.return_codes);

    /* Free the packet ID - SUBACK received successfully */
    mqtt_client_free_packet_id(client, packet_id);
    mqtt_buffer_reset(&client->recv_buf);

    /* Return error if any subscription was rejected */
    if (has_failure) {
        client->last_error = MQTT_ERR_SUBSCRIPTION_NOT_FOUND;
        return MQTT_ERR_SUBSCRIPTION_NOT_FOUND;
    }

    return MQTT_OK;
}

/* ========================================================================== */
/* Synchronous Unsubscribe Function                                           */
/* ========================================================================== */

mqtt_error_t mqtt_unsubscribe(mqtt_client_t *client, const char **topic_filters, size_t count)
{
    if (!client || !topic_filters || count == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (client->state != MQTT_STATE_CONNECTED) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    mqtt_error_t err;

    /* Allocate packet ID */
    uint16_t packet_id;
    err = mqtt_client_alloc_packet_id(client, &packet_id);
    if (err != MQTT_OK) {
        client->last_error = err;
        return err;
    }

    /* Build UNSUBSCRIBE packet */
    mqtt_buffer_reset(&client->send_buf);
    ssize_t encoded = mqtt_v311_encode_unsubscribe(&client->send_buf, packet_id,
                                                    topic_filters, count);
    if (encoded < 0) {
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = (mqtt_error_t)encoded;
        return (mqtt_error_t)encoded;
    }

    /* Send UNSUBSCRIBE packet */
    err = mqtt_client_send_packet(client);
    if (err != MQTT_OK) {
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = err;
        return err;
    }

    /* Wait for UNSUBACK */
    err = mqtt_client_recv_packet(client, 5000);  /* 5 second timeout */
    if (err != MQTT_OK) {
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = err;
        return err;
    }

    /* Parse UNSUBACK */
    const uint8_t *data = mqtt_buffer_data_const(&client->recv_buf);
    size_t len = mqtt_buffer_len(&client->recv_buf);

    if (len < 2) {
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = MQTT_ERR_MALFORMED_PACKET;
        return MQTT_ERR_MALFORMED_PACKET;
    }

    uint8_t packet_type = (data[0] >> 4) & 0x0F;
    if (packet_type != MQTT_PACKET_UNSUBACK) {
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = MQTT_ERR_PROTOCOL;
        return MQTT_ERR_PROTOCOL;
    }

    /* Verify packet ID matches */
    uint32_t remaining_len;
    int varint_bytes = mqtt_varint_decode(data + 1, len - 1, &remaining_len);
    if (varint_bytes < 0 || remaining_len < 2) {
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = MQTT_ERR_MALFORMED_PACKET;
        return MQTT_ERR_MALFORMED_PACKET;
    }

    size_t header_len = 1 + varint_bytes;
    uint16_t recv_packet_id = (data[header_len] << 8) | data[header_len + 1];

    if (recv_packet_id != packet_id) {
        mqtt_client_free_packet_id(client, packet_id);
        client->last_error = MQTT_ERR_PROTOCOL;
        return MQTT_ERR_PROTOCOL;
    }

    /* Free the packet ID - UNSUBACK received successfully */
    mqtt_client_free_packet_id(client, packet_id);
    mqtt_buffer_reset(&client->recv_buf);

    return MQTT_OK;
}

/* ========================================================================== */
/* QoS Acknowledgment Handlers                                                */
/* ========================================================================== */

mqtt_error_t mqtt_client_handle_puback(mqtt_client_t *client, uint16_t packet_id)
{
    if (!client) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Find the inflight message */
    mqtt_inflight_entry_t *entry = mqtt_inflight_find(&client->inflight, packet_id);
    if (!entry) {
        /* Unknown packet ID - protocol violation or late ACK */
        return MQTT_ERR_INVALID_PACKET_ID;
    }

    /* Verify this is a QoS 1 message */
    if (entry->qos != MQTT_QOS_1) {
        return MQTT_ERR_PROTOCOL;
    }

    /* Free the packet ID */
    mqtt_client_free_packet_id(client, packet_id);

    /* Remove from inflight queue */
    mqtt_inflight_remove(&client->inflight, entry);

    /* Invoke callback */
    if (client->callbacks.on_publish_complete) {
        client->callbacks.on_publish_complete(client, client->callbacks.user_data, packet_id);
    }

    return MQTT_OK;
}

mqtt_error_t mqtt_client_handle_pubrec(mqtt_client_t *client, uint16_t packet_id)
{
    if (!client) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Find the inflight message */
    mqtt_inflight_entry_t *entry = mqtt_inflight_find(&client->inflight, packet_id);
    if (!entry) {
        return MQTT_ERR_INVALID_PACKET_ID;
    }

    /* Verify this is a QoS 2 message in PENDING state */
    if (entry->qos != MQTT_QOS_2 || entry->state != MQTT_INFLIGHT_PENDING) {
        return MQTT_ERR_PROTOCOL;
    }

    /* Send PUBREL */
    mqtt_buffer_reset(&client->send_buf);
    ssize_t encoded = mqtt_v311_encode_pubrel(&client->send_buf, packet_id);
    if (encoded < 0) {
        return (mqtt_error_t)encoded;
    }

    mqtt_error_t err = mqtt_client_send_packet(client);
    if (err != MQTT_OK) {
        return err;
    }

    /* Update state to PUBREL sent */
    mqtt_inflight_update_state(entry, MQTT_INFLIGHT_PUBREL, mqtt_time_monotonic_ms());

    return MQTT_OK;
}

mqtt_error_t mqtt_client_handle_pubrel(mqtt_client_t *client, uint16_t packet_id)
{
    /* This handles incoming PUBREL from broker (when we're the receiver of QoS 2) */
    if (!client) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Remove from QoS 2 receive tracker - transaction complete */
    mqtt_qos2_recv_remove(&client->qos2_recv, packet_id);

    /* Send PUBCOMP */
    mqtt_buffer_reset(&client->send_buf);
    ssize_t encoded = mqtt_v311_encode_pubcomp(&client->send_buf, packet_id);
    if (encoded < 0) {
        return (mqtt_error_t)encoded;
    }

    return mqtt_client_send_packet(client);
}

mqtt_error_t mqtt_client_handle_pubcomp(mqtt_client_t *client, uint16_t packet_id)
{
    if (!client) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Find the inflight message */
    mqtt_inflight_entry_t *entry = mqtt_inflight_find(&client->inflight, packet_id);
    if (!entry) {
        return MQTT_ERR_INVALID_PACKET_ID;
    }

    /* Verify this is a QoS 2 message in PUBREL state */
    if (entry->qos != MQTT_QOS_2 || entry->state != MQTT_INFLIGHT_PUBREL) {
        return MQTT_ERR_PROTOCOL;
    }

    /* Free the packet ID */
    mqtt_client_free_packet_id(client, packet_id);

    /* Remove from inflight queue */
    mqtt_inflight_remove(&client->inflight, entry);

    /* Invoke callback - QoS 2 publish complete! */
    if (client->callbacks.on_publish_complete) {
        client->callbacks.on_publish_complete(client, client->callbacks.user_data, packet_id);
    }

    return MQTT_OK;
}

/**
 * @brief Handle an incoming PUBLISH message
 *
 * Parses the PUBLISH packet, invokes the on_message callback,
 * and sends appropriate acknowledgment for QoS 1/2.
 */
static mqtt_error_t mqtt_client_handle_publish(mqtt_client_t *client,
                                                const uint8_t *data,
                                                size_t len)
{
    if (!client || !data || len < 2) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Extract flags from first byte */
    uint8_t flags = data[0] & 0x0F;

    /* Decode remaining length */
    uint32_t remaining_len;
    int varint_bytes = mqtt_varint_decode(data + 1, len - 1, &remaining_len);
    if (varint_bytes < 0) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    size_t header_len = 1 + varint_bytes;
    size_t total_len = header_len + remaining_len;

    /* Check if we have the full packet */
    if (len < total_len) {
        return MQTT_ERR_BUFFER_UNDERFLOW;  /* Need more data */
    }

    /* Decode the PUBLISH packet */
    mqtt_v311_publish_t pub;
    mqtt_error_t err = mqtt_v311_decode_publish(data + header_len, remaining_len, flags, &pub);
    if (err != MQTT_OK) {
        return err;
    }

    /* Handle QoS 2 duplicate detection */
    bool is_duplicate = false;
    if (pub.qos == MQTT_QOS_2) {
        if (mqtt_qos2_recv_is_tracked(&client->qos2_recv, pub.packet_id)) {
            /* Duplicate QoS 2 message - don't deliver again */
            is_duplicate = true;
        } else {
            /* New QoS 2 message - add to tracker */
            mqtt_qos2_recv_add(&client->qos2_recv, pub.packet_id);
        }
    }

    /* Only deliver message if it's not a duplicate */
    if (!is_duplicate) {
        /* Build mqtt_message_t for callback */
        mqtt_message_t msg = {0};
        msg.topic = pub.topic;
        msg.topic_len = pub.topic_len;
        msg.payload = pub.payload;
        msg.payload_len = pub.payload_len;
        msg.qos = pub.qos;
        msg.retain = pub.retain;
        msg.dup = pub.dup;
        msg.packet_id = pub.packet_id;

        /* Invoke on_message callback */
        if (client->callbacks.on_message) {
            client->callbacks.on_message(client, client->callbacks.user_data, &msg);
        }
    }

    /* Send acknowledgment based on QoS */
    if (pub.qos == MQTT_QOS_1) {
        /* Send PUBACK */
        mqtt_buffer_reset(&client->send_buf);
        ssize_t encoded = mqtt_v311_encode_puback(&client->send_buf, pub.packet_id);
        if (encoded > 0) {
            mqtt_client_send_packet(client);
        }
    } else if (pub.qos == MQTT_QOS_2) {
        /* Send PUBREC (even for duplicates - broker needs confirmation) */
        mqtt_buffer_reset(&client->send_buf);
        ssize_t encoded = mqtt_v311_encode_pubrec(&client->send_buf, pub.packet_id);
        if (encoded > 0) {
            mqtt_client_send_packet(client);
        }
    }

    return MQTT_OK;
}

mqtt_error_t mqtt_client_process_retries(mqtt_client_t *client)
{
    if (!client) {
        return MQTT_ERR_INVALID_ARG;
    }

    uint64_t now = mqtt_time_monotonic_ms();

    /* Check for messages that need retry */
    mqtt_inflight_entry_t *entry;
    while ((entry = mqtt_inflight_get_retry(&client->inflight, now)) != NULL) {
        mqtt_error_t err;

        switch (entry->state) {
            case MQTT_INFLIGHT_PENDING:
                /* Retry PUBLISH with DUP flag */
                {
                    mqtt_publish_opts_t opts = {
                        .topic = entry->topic,
                        .payload = entry->payload,
                        .payload_len = entry->payload_len,
                        .qos = entry->qos,
                        .retain = entry->retain,
                        .dup = true  /* Set DUP flag for retransmission */
                    };

                    mqtt_buffer_reset(&client->send_buf);
                    ssize_t encoded = mqtt_v311_encode_publish(&client->send_buf, &opts, entry->packet_id);
                    if (encoded < 0) {
                        return (mqtt_error_t)encoded;
                    }

                    err = mqtt_client_send_packet(client);
                    if (err != MQTT_OK) {
                        return err;
                    }
                }
                break;

            case MQTT_INFLIGHT_PUBREL:
                /* Retry PUBREL */
                mqtt_buffer_reset(&client->send_buf);
                ssize_t encoded = mqtt_v311_encode_pubrel(&client->send_buf, entry->packet_id);
                if (encoded < 0) {
                    return (mqtt_error_t)encoded;
                }

                err = mqtt_client_send_packet(client);
                if (err != MQTT_OK) {
                    return err;
                }
                break;

            default:
                /* PUBREC state shouldn't happen - we transition immediately to PUBREL */
                break;
        }

        /* Mark as retried */
        mqtt_inflight_mark_retried(entry, now);
    }

    /* Check for messages that have exceeded max retries */
    mqtt_inflight_entry_t *expired;
    while ((expired = mqtt_inflight_get_expired(&client->inflight)) != NULL) {
        uint16_t packet_id = expired->packet_id;

        /* Free the packet ID */
        mqtt_client_free_packet_id(client, packet_id);

        /* Remove from queue (frees the entry) */
        mqtt_inflight_remove(&client->inflight, expired);

        /* Invoke the on_publish_failed callback */
        if (client->callbacks.on_publish_failed) {
            client->callbacks.on_publish_failed(client, client->callbacks.user_data,
                                                 packet_id, MQTT_ERR_MAX_RETRIES);
        }
    }

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
    uint64_t now = mqtt_time_monotonic_ms();

    /* Process message retries for QoS 1/2 */
    err = mqtt_client_process_retries(client);
    if (err != MQTT_OK) {
        return err;
    }

    /* Check if we need to send PINGREQ */
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
        /* No new data, but process any packets already in buffer */
    } else if (err != MQTT_OK) {
        return err;
    }

    /* Process all complete packets in buffer */
    while (1) {
        const uint8_t *data = mqtt_buffer_data_const(&client->recv_buf);
        size_t len = mqtt_buffer_len(&client->recv_buf);

        if (len < 2) {
            break;  /* Not enough data for a packet */
        }

        /* Decode remaining length to find packet size */
        uint32_t remaining_len;
        int varint_bytes = mqtt_varint_decode(data + 1, len - 1, &remaining_len);
        if (varint_bytes < 0) {
            break;  /* Incomplete varint, need more data */
        }

        size_t packet_len = 1 + varint_bytes + remaining_len;
        if (len < packet_len) {
            break;  /* Incomplete packet, need more data */
        }

        uint8_t packet_type = (data[0] >> 4) & 0x0F;

        switch (packet_type) {
            case MQTT_PACKET_PUBLISH:
                mqtt_client_handle_publish(client, data, packet_len);
                break;

            case MQTT_PACKET_PUBACK:
                /* QoS 1 acknowledgment - packet is: type(1) + len(1) + packet_id(2) */
                if (packet_len >= 4) {
                    size_t header_len = 1 + varint_bytes;
                    uint16_t packet_id = (data[header_len] << 8) | data[header_len + 1];
                    mqtt_client_handle_puback(client, packet_id);
                }
                break;

            case MQTT_PACKET_PUBREC:
                /* QoS 2 step 1: PUBLISH received */
                if (packet_len >= 4) {
                    size_t header_len = 1 + varint_bytes;
                    uint16_t packet_id = (data[header_len] << 8) | data[header_len + 1];
                    mqtt_client_handle_pubrec(client, packet_id);
                }
                break;

            case MQTT_PACKET_PUBREL:
                /* QoS 2 step 2: PUBLISH release (from broker to us as receiver) */
                if (packet_len >= 4) {
                    size_t header_len = 1 + varint_bytes;
                    uint16_t packet_id = (data[header_len] << 8) | data[header_len + 1];
                    mqtt_client_handle_pubrel(client, packet_id);
                }
                break;

            case MQTT_PACKET_PUBCOMP:
                /* QoS 2 step 3: PUBLISH complete */
                if (packet_len >= 4) {
                    size_t header_len = 1 + varint_bytes;
                    uint16_t packet_id = (data[header_len] << 8) | data[header_len + 1];
                    mqtt_client_handle_pubcomp(client, packet_id);
                }
                break;

            case MQTT_PACKET_PINGRESP:
                client->ping_outstanding = false;
                break;

            default:
                /* Unknown or unexpected packet - skip it */
                break;
        }

        /* Consume the processed packet from buffer */
        mqtt_buffer_consume(&client->recv_buf, packet_len);
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
