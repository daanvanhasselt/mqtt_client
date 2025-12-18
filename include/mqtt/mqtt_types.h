/**
 * @file mqtt_types.h
 * @brief Public types and structures for MQTT client library
 */

#ifndef MQTT_TYPES_H
#define MQTT_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "mqtt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Enumerations                                                               */
/* ========================================================================== */

/**
 * @brief MQTT Quality of Service levels
 */
typedef enum {
    MQTT_QOS_0 = 0,  /**< At most once delivery */
    MQTT_QOS_1 = 1,  /**< At least once delivery */
    MQTT_QOS_2 = 2   /**< Exactly once delivery */
} mqtt_qos_t;

/**
 * @brief MQTT protocol versions
 */
typedef enum {
    MQTT_VERSION_3_1_1 = 4,  /**< MQTT 3.1.1 */
    MQTT_VERSION_5_0 = 5     /**< MQTT 5.0 */
} mqtt_protocol_version_t;

/**
 * @brief Transport layer types
 */
typedef enum {
    MQTT_TRANSPORT_TCP = 0,  /**< Plain TCP */
    MQTT_TRANSPORT_TLS,      /**< TLS/SSL over TCP */
    MQTT_TRANSPORT_WS,       /**< WebSocket */
    MQTT_TRANSPORT_WSS       /**< WebSocket Secure (TLS) */
} mqtt_transport_type_t;

/**
 * @brief MQTT 5.0 retain handling options
 */
typedef enum {
    MQTT_RETAIN_SEND_ON_SUBSCRIBE = 0,  /**< Send retained messages on subscribe */
    MQTT_RETAIN_SEND_IF_NEW_SUB = 1,    /**< Send retained messages only if subscription doesn't exist */
    MQTT_RETAIN_DO_NOT_SEND = 2         /**< Do not send retained messages */
} mqtt_retain_handling_t;

/**
 * @brief MQTT 5.0 payload format indicator
 */
typedef enum {
    MQTT_PAYLOAD_FORMAT_BINARY = 0,  /**< Binary/unspecified format */
    MQTT_PAYLOAD_FORMAT_UTF8 = 1     /**< UTF-8 encoded character data */
} mqtt_payload_format_t;

/* ========================================================================== */
/* Forward Declarations                                                       */
/* ========================================================================== */

/**
 * @brief Opaque MQTT client handle
 */
typedef struct mqtt_client mqtt_client_t;

/**
 * @brief MQTT 5.0 property structure
 */
typedef struct mqtt_property mqtt_property_t;

/* ========================================================================== */
/* Configuration Structures                                                   */
/* ========================================================================== */

/**
 * @brief TLS/SSL configuration
 */
typedef struct {
    const char *ca_cert_path;          /**< Path to CA certificate file (PEM) */
    const uint8_t *ca_cert_buffer;     /**< CA certificate buffer (alternative to path) */
    size_t ca_cert_len;                /**< Length of CA certificate buffer */

    const char *client_cert_path;      /**< Path to client certificate file (PEM) */
    const char *client_key_path;       /**< Path to client private key file (PEM) */

    const char **alpn_protocols;       /**< NULL-terminated array of ALPN protocol names */

    bool verify_peer;                  /**< Verify server certificate */
    bool verify_hostname;              /**< Verify server hostname */
    uint16_t min_tls_version;          /**< Minimum TLS version (e.g., 0x0303 for TLS 1.2) */
} mqtt_tls_config_t;

/**
 * @brief HTTP proxy configuration for WebSocket connections
 */
typedef struct mqtt_proxy_config {
    const char *host;                  /**< Proxy hostname or IP */
    uint16_t port;                     /**< Proxy port (default: 80 for HTTP, 443 for HTTPS) */
    const char *username;              /**< Proxy authentication username (NULL for none) */
    const char *password;              /**< Proxy authentication password (NULL for none) */
    bool use_tls;                      /**< Use TLS to connect to proxy (HTTPS proxy) */
} mqtt_proxy_config_t;

/**
 * @brief WebSocket configuration
 */
typedef struct {
    const char *path;                  /**< WebSocket path (default: "/mqtt") */
    const char *subprotocol;           /**< WebSocket subprotocol (e.g., "mqtt") */
    const char **extra_headers;        /**< NULL-terminated array of extra HTTP headers */
    mqtt_proxy_config_t *proxy;        /**< HTTP CONNECT proxy configuration (NULL for direct) */
} mqtt_ws_config_t;

/**
 * @brief Last Will and Testament message
 */
typedef struct {
    const char *topic;                 /**< Will topic */
    const uint8_t *payload;            /**< Will message payload */
    size_t payload_len;                /**< Length of will payload */
    mqtt_qos_t qos;                    /**< Will QoS level */
    bool retain;                       /**< Will retain flag */

    /* MQTT 5.0 specific */
    uint32_t delay_interval;           /**< Will delay interval in seconds (0 = send immediately) */
    const char *content_type;          /**< Will message content type */
} mqtt_will_message_t;

/**
 * @brief MQTT connection options
 */
typedef struct {
    /* Connection parameters */
    const char *host;                  /**< MQTT broker hostname/IP */
    uint16_t port;                     /**< MQTT broker port (default: 1883 for TCP, 8883 for TLS) */
    const char *client_id;             /**< Client identifier (NULL for auto-generated) */

    /* Authentication */
    const char *username;              /**< Username (NULL if not used) */
    const uint8_t *password;           /**< Password (can contain binary data) */
    size_t password_len;               /**< Password length (0 if password is NULL-terminated string) */

    /* Session parameters */
    uint16_t keepalive_sec;            /**< Keep-alive interval in seconds (default: 60) */
    bool clean_session;                /**< MQTT 3.1.1: Clean session flag */
    bool clean_start;                  /**< MQTT 5.0: Clean start flag */
    mqtt_protocol_version_t protocol_version; /**< MQTT protocol version */

    /* Last Will and Testament */
    mqtt_will_message_t *will;         /**< Will message (NULL if not used) */

    /* Transport configuration */
    mqtt_transport_type_t transport_type; /**< Transport layer type */
    mqtt_tls_config_t *tls_config;     /**< TLS configuration (required for TLS/WSS) */
    mqtt_ws_config_t *ws_config;       /**< WebSocket configuration (optional for WS/WSS) */

    /* Timeouts */
    uint32_t connect_timeout_ms;       /**< Connection timeout in milliseconds (default: 30000) */
    uint32_t socket_timeout_ms;        /**< Socket I/O timeout in milliseconds (default: 5000) */

    /* MQTT 5.0 specific options */
    uint32_t session_expiry_interval;  /**< Session expiry interval in seconds (0 = expire on disconnect) */
    uint16_t receive_maximum;          /**< Maximum number of QoS 1 and 2 messages (default: 65535) */
    uint32_t maximum_packet_size;      /**< Maximum packet size client is willing to accept (0 = no limit) */
    uint16_t topic_alias_maximum;      /**< Maximum topic alias value (0 = no topic aliases) */
    mqtt_property_t *user_properties;  /**< User properties (linked list) */
} mqtt_connect_opts_t;

/**
 * @brief MQTT publish options
 */
typedef struct {
    const char *topic;                 /**< Publish topic */
    const uint8_t *payload;            /**< Message payload */
    size_t payload_len;                /**< Payload length */
    mqtt_qos_t qos;                    /**< Quality of Service level */
    bool retain;                       /**< Retain flag */
    bool dup;                          /**< Duplicate flag (set internally for retransmissions) */

    /* MQTT 5.0 specific options */
    mqtt_payload_format_t payload_format; /**< Payload format indicator */
    uint32_t message_expiry;           /**< Message expiry interval in seconds (0 = no expiry) */
    const char *response_topic;        /**< Response topic */
    const uint8_t *correlation_data;   /**< Correlation data */
    size_t correlation_data_len;       /**< Correlation data length */
    const char *content_type;          /**< Content type */
    uint16_t topic_alias;              /**< Topic alias (0 = not used) */
    mqtt_property_t *user_properties;  /**< User properties (linked list) */
} mqtt_publish_opts_t;

/**
 * @brief MQTT subscribe options
 */
typedef struct {
    const char *topic_filter;          /**< Topic filter to subscribe to */
    mqtt_qos_t max_qos;                /**< Maximum QoS level */

    /* MQTT 5.0 specific options */
    bool no_local;                     /**< No local flag (don't receive own publishes) */
    bool retain_as_published;          /**< Retain as published flag */
    mqtt_retain_handling_t retain_handling; /**< Retain handling option */
    uint32_t subscription_id;          /**< Subscription identifier (0 = not used) */
    mqtt_property_t *user_properties;  /**< User properties (linked list) */
} mqtt_subscribe_opts_t;

/**
 * @brief Received MQTT message
 */
typedef struct {
    const char *topic;                 /**< Message topic */
    size_t topic_len;                  /**< Topic length */
    const uint8_t *payload;            /**< Message payload */
    size_t payload_len;                /**< Payload length */
    mqtt_qos_t qos;                    /**< Quality of Service level */
    bool retain;                       /**< Retain flag */
    bool dup;                          /**< Duplicate flag */
    uint16_t packet_id;                /**< Packet identifier (for QoS > 0) */

    /* MQTT 5.0 specific fields */
    uint32_t subscription_id;          /**< Subscription identifier */
    mqtt_payload_format_t payload_format; /**< Payload format indicator */
    uint32_t message_expiry;           /**< Message expiry interval */
    const char *response_topic;        /**< Response topic */
    const uint8_t *correlation_data;   /**< Correlation data */
    size_t correlation_data_len;       /**< Correlation data length */
    const char *content_type;          /**< Content type */
    mqtt_property_t *user_properties;  /**< User properties (linked list) */
} mqtt_message_t;

/* ========================================================================== */
/* Callback Function Types                                                    */
/* ========================================================================== */

/**
 * @brief Connection established callback
 *
 * @param client MQTT client handle
 * @param user_data User-provided context data
 * @param session_present Session present flag (MQTT 3.1.1+)
 */
typedef void (*mqtt_on_connect_cb)(mqtt_client_t *client, void *user_data, bool session_present);

/**
 * @brief Disconnection callback
 *
 * @param client MQTT client handle
 * @param user_data User-provided context data
 * @param reason_code Reason code for disconnection
 */
typedef void (*mqtt_on_disconnect_cb)(mqtt_client_t *client, void *user_data, int reason_code);

/**
 * @brief Message received callback
 *
 * @param client MQTT client handle
 * @param user_data User-provided context data
 * @param message Received message
 */
typedef void (*mqtt_on_message_cb)(mqtt_client_t *client, void *user_data, const mqtt_message_t *message);

/**
 * @brief Publish complete callback (for QoS 1 and 2)
 *
 * @param client MQTT client handle
 * @param user_data User-provided context data
 * @param packet_id Packet identifier of completed publish
 */
typedef void (*mqtt_on_publish_complete_cb)(mqtt_client_t *client, void *user_data, uint16_t packet_id);

/**
 * @brief Publish failed callback (for QoS 1 and 2 when max retries exceeded)
 *
 * @param client MQTT client handle
 * @param user_data User-provided context data
 * @param packet_id Packet identifier of failed publish
 * @param reason Error code indicating why the publish failed
 */
typedef void (*mqtt_on_publish_failed_cb)(mqtt_client_t *client, void *user_data, uint16_t packet_id, mqtt_error_t reason);

/**
 * @brief Subscribe complete callback
 *
 * @param client MQTT client handle
 * @param user_data User-provided context data
 * @param packet_id Packet identifier of subscribe request
 * @param granted_qos Array of granted QoS levels
 * @param count Number of subscriptions
 */
typedef void (*mqtt_on_subscribe_cb)(mqtt_client_t *client, void *user_data, uint16_t packet_id,
                                     const mqtt_qos_t *granted_qos, size_t count);

/**
 * @brief Server redirect callback (MQTT 5.0)
 *
 * Called when the server requests the client to connect to a different server.
 * This happens with reason codes 0x9C (Use Another Server) or 0x9D (Server Moved).
 *
 * @param client MQTT client handle
 * @param user_data User-provided context data
 * @param server_reference New server address (e.g., "host:port" or "host")
 * @param is_permanent true if server moved permanently (0x9D), false if temporary (0x9C)
 */
typedef void (*mqtt_on_redirect_cb)(mqtt_client_t *client, void *user_data,
                                     const char *server_reference, bool is_permanent);

/**
 * @brief Callback functions bundle
 */
typedef struct {
    mqtt_on_connect_cb on_connect;                /**< Connection callback */
    mqtt_on_disconnect_cb on_disconnect;          /**< Disconnection callback */
    mqtt_on_message_cb on_message;                /**< Message received callback */
    mqtt_on_publish_complete_cb on_publish_complete; /**< Publish complete callback */
    mqtt_on_publish_failed_cb on_publish_failed;  /**< Publish failed callback */
    mqtt_on_subscribe_cb on_subscribe;            /**< Subscribe complete callback */
    mqtt_on_redirect_cb on_redirect;              /**< Server redirect callback (MQTT 5.0) */
    void *user_data;                              /**< User-provided context data */
} mqtt_callbacks_t;

#ifdef __cplusplus
}
#endif

#endif /* MQTT_TYPES_H */
