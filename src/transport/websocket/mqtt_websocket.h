/**
 * @file mqtt_websocket.h
 * @brief WebSocket Transport for MQTT
 *
 * Implements WebSocket protocol (RFC 6455) for MQTT transport.
 * Supports both ws:// (plain) and wss:// (TLS) connections.
 */

#ifndef MQTT_WEBSOCKET_H
#define MQTT_WEBSOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <mqtt/mqtt_types.h>
#include <mqtt/mqtt_error.h>
#include "../../memory/mqtt_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* WebSocket Frame Types                                                       */
/* ========================================================================== */

/**
 * @brief WebSocket frame opcodes (RFC 6455 Section 5.2)
 */
typedef enum ws_opcode {
    WS_OPCODE_CONTINUATION = 0x0,  /**< Continuation frame */
    WS_OPCODE_TEXT         = 0x1,  /**< Text frame */
    WS_OPCODE_BINARY       = 0x2,  /**< Binary frame */
    WS_OPCODE_CLOSE        = 0x8,  /**< Connection close */
    WS_OPCODE_PING         = 0x9,  /**< Ping frame */
    WS_OPCODE_PONG         = 0xA   /**< Pong frame */
} ws_opcode_t;

/**
 * @brief WebSocket close status codes (RFC 6455 Section 7.4.1)
 */
typedef enum ws_close_code {
    WS_CLOSE_NORMAL           = 1000,  /**< Normal closure */
    WS_CLOSE_GOING_AWAY       = 1001,  /**< Endpoint going away */
    WS_CLOSE_PROTOCOL_ERROR   = 1002,  /**< Protocol error */
    WS_CLOSE_UNSUPPORTED_DATA = 1003,  /**< Unsupported data type */
    WS_CLOSE_NO_STATUS        = 1005,  /**< No status received (reserved) */
    WS_CLOSE_ABNORMAL         = 1006,  /**< Abnormal closure (reserved) */
    WS_CLOSE_INVALID_PAYLOAD  = 1007,  /**< Invalid frame payload data */
    WS_CLOSE_POLICY_VIOLATION = 1008,  /**< Policy violation */
    WS_CLOSE_MESSAGE_TOO_BIG  = 1009,  /**< Message too big */
    WS_CLOSE_EXTENSION_NEEDED = 1010,  /**< Mandatory extension missing */
    WS_CLOSE_INTERNAL_ERROR   = 1011,  /**< Internal server error */
    WS_CLOSE_TLS_HANDSHAKE    = 1015   /**< TLS handshake failure (reserved) */
} ws_close_code_t;

/* ========================================================================== */
/* WebSocket Frame Structure                                                   */
/* ========================================================================== */

/**
 * @brief Parsed WebSocket frame header
 */
typedef struct ws_frame_header {
    bool fin;               /**< Final fragment flag */
    bool rsv1;              /**< Reserved bit 1 (must be 0) */
    bool rsv2;              /**< Reserved bit 2 (must be 0) */
    bool rsv3;              /**< Reserved bit 3 (must be 0) */
    ws_opcode_t opcode;     /**< Frame opcode */
    bool masked;            /**< Masking flag */
    uint64_t payload_len;   /**< Payload length */
    uint8_t mask_key[4];    /**< Masking key (if masked) */
    size_t header_len;      /**< Total header length in bytes */
} ws_frame_header_t;

/* ========================================================================== */
/* WebSocket Connection State                                                  */
/* ========================================================================== */

/**
 * @brief WebSocket connection state
 */
typedef enum ws_state {
    WS_STATE_DISCONNECTED,     /**< Not connected */
    WS_STATE_CONNECTING,       /**< HTTP upgrade in progress */
    WS_STATE_CONNECTED,        /**< WebSocket connected */
    WS_STATE_CLOSING,          /**< Close handshake in progress */
    WS_STATE_CLOSED            /**< Connection closed */
} ws_state_t;

/**
 * @brief WebSocket connection context
 */
typedef struct ws_connection {
    ws_state_t state;          /**< Connection state */

    /* Handshake data */
    char sec_key[25];          /**< Base64 Sec-WebSocket-Key (24 chars + null) */
    char expected_accept[29];  /**< Expected Sec-WebSocket-Accept (28 chars + null) */

    /* Frame assembly */
    mqtt_buffer_t frame_buf;   /**< Buffer for incoming frame data */
    bool in_fragment;          /**< Currently receiving fragmented message */
    ws_opcode_t fragment_opcode; /**< Opcode of fragmented message */

    /* Configuration */
    const char *path;          /**< WebSocket path (e.g., "/mqtt") */
    const char *host;          /**< Host header value */
    const char *subprotocol;   /**< Negotiated subprotocol */
    const char **extra_headers; /**< Extra HTTP headers (NULL-terminated) */
} ws_connection_t;

/* ========================================================================== */
/* Frame Encoding/Decoding                                                     */
/* ========================================================================== */

/**
 * @brief Calculate encoded frame size
 *
 * @param payload_len Payload length
 * @param masked Whether frame will be masked (client->server must be masked)
 * @return Total frame size including header
 */
size_t ws_frame_size(size_t payload_len, bool masked);

/**
 * @brief Encode a WebSocket frame
 *
 * @param buf Output buffer
 * @param opcode Frame opcode
 * @param payload Payload data (can be NULL if payload_len is 0)
 * @param payload_len Payload length
 * @param fin Final fragment flag
 * @param masked Whether to mask the payload (client->server must be masked)
 * @return Number of bytes written, or negative error code
 */
ssize_t ws_frame_encode(mqtt_buffer_t *buf, ws_opcode_t opcode,
                        const uint8_t *payload, size_t payload_len,
                        bool fin, bool masked);

/**
 * @brief Decode a WebSocket frame header
 *
 * @param data Input data
 * @param len Available data length
 * @param header Output header structure
 * @return Number of header bytes consumed, 0 if incomplete, negative on error
 */
int ws_frame_decode_header(const uint8_t *data, size_t len, ws_frame_header_t *header);

/**
 * @brief Unmask WebSocket payload data in-place
 *
 * @param data Payload data to unmask
 * @param len Payload length
 * @param mask_key 4-byte masking key
 */
void ws_frame_unmask(uint8_t *data, size_t len, const uint8_t *mask_key);

/* ========================================================================== */
/* HTTP Upgrade Handshake                                                      */
/* ========================================================================== */

/**
 * @brief Generate random Sec-WebSocket-Key
 *
 * @param key Output buffer (must be at least 25 bytes)
 */
void ws_generate_key(char *key);

/**
 * @brief Calculate expected Sec-WebSocket-Accept value
 *
 * @param key Client's Sec-WebSocket-Key
 * @param accept Output buffer (must be at least 29 bytes)
 */
void ws_calculate_accept(const char *key, char *accept);

/**
 * @brief Build HTTP upgrade request
 *
 * @param buf Output buffer
 * @param host Host header value
 * @param path Request path (e.g., "/mqtt")
 * @param key Sec-WebSocket-Key (from ws_generate_key)
 * @param subprotocol Requested subprotocol (e.g., "mqtt")
 * @param extra_headers NULL-terminated array of extra headers (can be NULL)
 * @return Number of bytes written, or negative error code
 */
ssize_t ws_build_upgrade_request(mqtt_buffer_t *buf, const char *host,
                                  const char *path, const char *key,
                                  const char *subprotocol,
                                  const char **extra_headers);

/**
 * @brief Parse HTTP upgrade response
 *
 * @param data Response data
 * @param len Response length
 * @param expected_accept Expected Sec-WebSocket-Accept value
 * @param subprotocol Output: negotiated subprotocol (can be NULL)
 * @param subprotocol_len Size of subprotocol buffer
 * @return Bytes consumed on success, 0 if incomplete, negative on error
 */
int ws_parse_upgrade_response(const uint8_t *data, size_t len,
                               const char *expected_accept,
                               char *subprotocol, size_t subprotocol_len);

/* ========================================================================== */
/* WebSocket Connection Management                                             */
/* ========================================================================== */

/**
 * @brief Initialize WebSocket connection context
 *
 * @param conn Connection context to initialize
 * @param path WebSocket path (e.g., "/mqtt")
 * @param host Host header value
 * @param subprotocol Requested subprotocol (e.g., "mqtt")
 * @param extra_headers NULL-terminated array of extra headers (can be NULL)
 * @return MQTT_OK on success
 */
mqtt_error_t ws_connection_init(ws_connection_t *conn, const char *path,
                                 const char *host, const char *subprotocol,
                                 const char **extra_headers);

/**
 * @brief Cleanup WebSocket connection context
 *
 * @param conn Connection context to cleanup
 */
void ws_connection_cleanup(ws_connection_t *conn);

/**
 * @brief Build close frame
 *
 * @param buf Output buffer
 * @param code Close status code
 * @param reason Optional close reason (can be NULL)
 * @return Number of bytes written, or negative error code
 */
ssize_t ws_build_close_frame(mqtt_buffer_t *buf, ws_close_code_t code, const char *reason);

/**
 * @brief Build ping frame
 *
 * @param buf Output buffer
 * @param data Optional ping data (can be NULL)
 * @param len Ping data length
 * @return Number of bytes written, or negative error code
 */
ssize_t ws_build_ping_frame(mqtt_buffer_t *buf, const uint8_t *data, size_t len);

/**
 * @brief Build pong frame
 *
 * @param buf Output buffer
 * @param data Pong data (should echo ping data)
 * @param len Pong data length
 * @return Number of bytes written, or negative error code
 */
ssize_t ws_build_pong_frame(mqtt_buffer_t *buf, const uint8_t *data, size_t len);

/* ========================================================================== */
/* HTTP CONNECT Proxy Support                                                  */
/* ========================================================================== */

/**
 * @brief Build HTTP CONNECT request for proxy tunneling
 *
 * @param buf Output buffer
 * @param target_host Target hostname to connect to through proxy
 * @param target_port Target port number
 * @param username Proxy username (NULL for no auth)
 * @param password Proxy password (NULL for no auth)
 * @return Number of bytes written, or negative error code
 */
ssize_t ws_build_proxy_connect(mqtt_buffer_t *buf, const char *target_host,
                                uint16_t target_port, const char *username,
                                const char *password);

/**
 * @brief Parse HTTP CONNECT response from proxy
 *
 * @param data Response data
 * @param len Response length
 * @return Bytes consumed on success (status 200), 0 if incomplete, negative on error
 */
int ws_parse_proxy_response(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_WEBSOCKET_H */
