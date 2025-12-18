#ifndef MQTT_CLIENT_INTERNAL_H
#define MQTT_CLIENT_INTERNAL_H

#include "mqtt/mqtt.h"
#include "mqtt/mqtt_types.h"
#include "mqtt/mqtt_error.h"
#include "../transport/mqtt_transport.h"
#include "../memory/mqtt_buffer.h"
#include "../platform/mqtt_platform.h"
#include "mqtt_packet_id.h"
#include "mqtt_inflight.h"
#include "mqtt_qos2_recv.h"
#include "mqtt_subscription_store.h"

/* Client state */
typedef enum mqtt_client_state {
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTING,       /* TCP/TLS connecting */
    MQTT_STATE_CONNECTED_TCP,    /* Transport connected, MQTT handshake pending */
    MQTT_STATE_CONNECTING_MQTT,  /* CONNECT sent, awaiting CONNACK */
    MQTT_STATE_CONNECTED,        /* Fully connected */
    MQTT_STATE_DISCONNECTING,
    MQTT_STATE_ERROR
} mqtt_client_state_t;

/* Internal client structure */
struct mqtt_client {
    /* Configuration */
    mqtt_client_config_t config;

    /* State */
    mqtt_client_state_t state;
    mqtt_protocol_version_t protocol_version;
    bool clean_session;

    /* Transport */
    mqtt_transport_t *transport;

    /* I/O buffers */
    mqtt_buffer_t send_buf;
    mqtt_buffer_t recv_buf;
    size_t recv_offset;  /* Parse position in recv_buf */

    /* QoS management (Phase 2) */
    mqtt_packet_id_allocator_t packet_ids;   /* Packet ID allocator */
    mqtt_inflight_queue_t inflight;          /* Inflight message queue for outgoing QoS 1/2 */
    mqtt_qos2_recv_tracker_t qos2_recv;      /* Received QoS 2 message tracker */

    /* Subscription tracking (for session restoration) */
    mqtt_subscription_store_t subscriptions; /* Active subscriptions */

    /* Connection info */
    uint16_t keepalive_sec;
    uint64_t last_send_time;
    uint64_t last_recv_time;
    bool ping_outstanding;

    /* Callbacks */
    mqtt_callbacks_t callbacks;

    /* Error state */
    mqtt_error_t last_error;

#ifdef MQTT_THREAD_SAFE
    mqtt_mutex_t lock;
#endif
};

/* Internal helpers - Packet management */
mqtt_error_t mqtt_client_alloc_packet_id(mqtt_client_t *client, uint16_t *packet_id);
void mqtt_client_free_packet_id(mqtt_client_t *client, uint16_t packet_id);

/* Internal helpers - I/O */
mqtt_error_t mqtt_client_send_packet(mqtt_client_t *client);
mqtt_error_t mqtt_client_recv_packet(mqtt_client_t *client, int timeout_ms);
void mqtt_client_update_last_send(mqtt_client_t *client);
void mqtt_client_update_last_recv(mqtt_client_t *client);

/* Internal helpers - QoS acknowledgment handling */
mqtt_error_t mqtt_client_handle_puback(mqtt_client_t *client, uint16_t packet_id);
mqtt_error_t mqtt_client_handle_pubrec(mqtt_client_t *client, uint16_t packet_id);
mqtt_error_t mqtt_client_handle_pubrel(mqtt_client_t *client, uint16_t packet_id);
mqtt_error_t mqtt_client_handle_pubcomp(mqtt_client_t *client, uint16_t packet_id);
mqtt_error_t mqtt_client_process_retries(mqtt_client_t *client);

#endif /* MQTT_CLIENT_INTERNAL_H */
