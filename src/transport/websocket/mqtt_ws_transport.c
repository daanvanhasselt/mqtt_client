/**
 * @file mqtt_ws_transport.c
 * @brief WebSocket Transport Layer Implementation
 *
 * Implements WebSocket transport for MQTT, wrapping an underlying TCP or TLS
 * transport with WebSocket framing.
 */

#define _POSIX_C_SOURCE 200809L

#include "../mqtt_transport.h"
#include "mqtt_websocket.h"
#include "../../memory/mqtt_memory.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ========================================================================== */
/* WebSocket Transport Structure                                               */
/* ========================================================================== */

/**
 * @brief WebSocket transport context
 */
typedef struct mqtt_ws_transport {
    mqtt_transport_t base;           /**< Base transport (must be first) */
    mqtt_transport_t *underlying;    /**< Underlying TCP or TLS transport */
    ws_connection_t ws_conn;         /**< WebSocket connection state */

    /* Configuration */
    mqtt_ws_config_t config;         /**< WebSocket configuration */
    char *host;                      /**< Host header value (owned) */

    /* Receive buffer for frame assembly */
    mqtt_buffer_t recv_buf;          /**< Raw data receive buffer */
    mqtt_buffer_t payload_buf;       /**< Extracted payload buffer */
    size_t payload_read_pos;         /**< Read position in payload buffer */

    /* Send buffer for frame encoding */
    mqtt_buffer_t send_buf;          /**< Frame encoding buffer */
} mqtt_ws_transport_t;

/* Forward declarations */
static mqtt_error_t ws_transport_connect(mqtt_transport_t *transport,
                                          const char *host, uint16_t port,
                                          uint32_t timeout_ms);
static mqtt_error_t ws_transport_disconnect(mqtt_transport_t *transport);
static ssize_t ws_transport_send(mqtt_transport_t *transport,
                                  const void *buf, size_t len);
static ssize_t ws_transport_recv(mqtt_transport_t *transport,
                                  void *buf, size_t len);
static int ws_transport_get_fd(mqtt_transport_t *transport);
static mqtt_error_t ws_transport_set_blocking(mqtt_transport_t *transport, bool blocking);
static void ws_transport_destroy(mqtt_transport_t *transport);

/* Operations vtable */
static const mqtt_transport_ops_t ws_transport_ops = {
    .connect = ws_transport_connect,
    .disconnect = ws_transport_disconnect,
    .send = ws_transport_send,
    .recv = ws_transport_recv,
    .get_fd = ws_transport_get_fd,
    .set_blocking = ws_transport_set_blocking,
    .destroy = ws_transport_destroy
};

/* ========================================================================== */
/* Transport Creation                                                          */
/* ========================================================================== */

mqtt_transport_t *mqtt_transport_ws_create(mqtt_transport_t *underlying,
                                            const mqtt_ws_config_t *config)
{
    if (!underlying) {
        return NULL;
    }

    mqtt_ws_transport_t *ws = mqtt_calloc(1, sizeof(mqtt_ws_transport_t));
    if (!ws) {
        return NULL;
    }

    /* Initialize base transport */
    ws->base.type = (underlying->type == MQTT_TRANSPORT_TLS) ?
                    MQTT_TRANSPORT_WSS : MQTT_TRANSPORT_WS;
    ws->base.status = MQTT_TRANSPORT_DISCONNECTED;
    ws->base.ops = &ws_transport_ops;
    ws->base.last_error = MQTT_OK;

    /* Store underlying transport */
    ws->underlying = underlying;

    /* Copy configuration */
    if (config) {
        ws->config.path = config->path;
        ws->config.subprotocol = config->subprotocol;
        ws->config.extra_headers = config->extra_headers;
    }

    /* Initialize buffers */
    mqtt_buffer_init(&ws->recv_buf, 8192);
    mqtt_buffer_init(&ws->payload_buf, 8192);
    mqtt_buffer_init(&ws->send_buf, 8192);

    return &ws->base;
}

/* ========================================================================== */
/* Connection Management                                                       */
/* ========================================================================== */

static mqtt_error_t ws_transport_connect(mqtt_transport_t *transport,
                                          const char *host, uint16_t port,
                                          uint32_t timeout_ms)
{
    mqtt_ws_transport_t *ws = (mqtt_ws_transport_t *)transport;
    mqtt_error_t err;

    if (!ws || !host) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Store host for HTTP headers */
    ws->host = mqtt_strdup(host);
    if (!ws->host) {
        return MQTT_ERR_NOMEM;
    }

    /* Connect underlying transport */
    err = mqtt_transport_connect(ws->underlying, host, port, timeout_ms);
    if (err != MQTT_OK) {
        mqtt_free(ws->host);
        ws->host = NULL;
        ws->base.last_error = err;
        return err;
    }

    ws->base.status = MQTT_TRANSPORT_CONNECTING;

    /* Initialize WebSocket connection */
    const char *path = ws->config.path ? ws->config.path : "/mqtt";
    const char *subprotocol = ws->config.subprotocol ? ws->config.subprotocol : "mqtt";

    err = ws_connection_init(&ws->ws_conn, path, ws->host, subprotocol,
                              ws->config.extra_headers);
    if (err != MQTT_OK) {
        mqtt_transport_disconnect(ws->underlying);
        mqtt_free(ws->host);
        ws->host = NULL;
        ws->base.last_error = err;
        return err;
    }

    /* Build HTTP upgrade request */
    mqtt_buffer_reset(&ws->send_buf);
    ssize_t req_len = ws_build_upgrade_request(&ws->send_buf, ws->host, path,
                                                ws->ws_conn.sec_key, subprotocol,
                                                ws->config.extra_headers);
    if (req_len < 0) {
        ws_connection_cleanup(&ws->ws_conn);
        mqtt_transport_disconnect(ws->underlying);
        mqtt_free(ws->host);
        ws->host = NULL;
        ws->base.last_error = (mqtt_error_t)(-req_len);
        return (mqtt_error_t)(-req_len);
    }

    /* Send upgrade request */
    ssize_t sent = mqtt_transport_send(ws->underlying, ws->send_buf.data, ws->send_buf.len);
    if (sent < 0 || (size_t)sent != ws->send_buf.len) {
        ws_connection_cleanup(&ws->ws_conn);
        mqtt_transport_disconnect(ws->underlying);
        mqtt_free(ws->host);
        ws->host = NULL;
        ws->base.last_error = (sent < 0) ? (mqtt_error_t)(-sent) : MQTT_ERR_SEND_FAILED;
        return ws->base.last_error;
    }

    /* Receive upgrade response */
    mqtt_buffer_reset(&ws->recv_buf);
    mqtt_buffer_reserve(&ws->recv_buf, 2048);

    int response_len = 0;
    while (response_len == 0) {
        ssize_t received = mqtt_transport_recv(ws->underlying,
                                                ws->recv_buf.data + ws->recv_buf.len,
                                                ws->recv_buf.capacity - ws->recv_buf.len);
        if (received <= 0) {
            ws_connection_cleanup(&ws->ws_conn);
            mqtt_transport_disconnect(ws->underlying);
            mqtt_free(ws->host);
            ws->host = NULL;
            ws->base.last_error = MQTT_ERR_CONNECTION_LOST;
            return MQTT_ERR_CONNECTION_LOST;
        }

        ws->recv_buf.len += (size_t)received;

        /* Try to parse response */
        char negotiated_subprotocol[64] = {0};
        response_len = ws_parse_upgrade_response(ws->recv_buf.data, ws->recv_buf.len,
                                                  ws->ws_conn.expected_accept,
                                                  negotiated_subprotocol,
                                                  sizeof(negotiated_subprotocol));
        if (response_len < 0) {
            ws_connection_cleanup(&ws->ws_conn);
            mqtt_transport_disconnect(ws->underlying);
            mqtt_free(ws->host);
            ws->host = NULL;
            ws->base.last_error = MQTT_ERR_PROTOCOL;
            ws->base.status = MQTT_TRANSPORT_ERROR;
            return MQTT_ERR_PROTOCOL;
        }
    }

    /* Remove consumed HTTP response from buffer */
    if ((size_t)response_len < ws->recv_buf.len) {
        size_t remaining = ws->recv_buf.len - (size_t)response_len;
        memmove(ws->recv_buf.data, ws->recv_buf.data + response_len, remaining);
        ws->recv_buf.len = remaining;
    } else {
        mqtt_buffer_reset(&ws->recv_buf);
    }

    ws->ws_conn.state = WS_STATE_CONNECTED;
    ws->base.status = MQTT_TRANSPORT_CONNECTED;

    return MQTT_OK;
}

static mqtt_error_t ws_transport_disconnect(mqtt_transport_t *transport)
{
    mqtt_ws_transport_t *ws = (mqtt_ws_transport_t *)transport;

    if (!ws) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (ws->base.status == MQTT_TRANSPORT_CONNECTED) {
        /* Send close frame */
        mqtt_buffer_reset(&ws->send_buf);
        ssize_t frame_len = ws_build_close_frame(&ws->send_buf, WS_CLOSE_NORMAL, NULL);
        if (frame_len > 0) {
            mqtt_transport_send(ws->underlying, ws->send_buf.data, ws->send_buf.len);
        }
    }

    /* Disconnect underlying transport */
    if (ws->underlying) {
        mqtt_transport_disconnect(ws->underlying);
    }

    ws_connection_cleanup(&ws->ws_conn);
    ws->base.status = MQTT_TRANSPORT_DISCONNECTED;

    return MQTT_OK;
}

/* ========================================================================== */
/* Data Transfer                                                               */
/* ========================================================================== */

static ssize_t ws_transport_send(mqtt_transport_t *transport,
                                  const void *buf, size_t len)
{
    mqtt_ws_transport_t *ws = (mqtt_ws_transport_t *)transport;

    if (!ws || !buf || len == 0) {
        return -MQTT_ERR_INVALID_ARG;
    }

    if (ws->base.status != MQTT_TRANSPORT_CONNECTED) {
        return -MQTT_ERR_NOT_CONNECTED;
    }

    /* Encode data in WebSocket binary frame */
    mqtt_buffer_reset(&ws->send_buf);
    ssize_t frame_len = ws_frame_encode(&ws->send_buf, WS_OPCODE_BINARY,
                                         (const uint8_t *)buf, len,
                                         true,   /* FIN = true */
                                         true);  /* Masked = true (client -> server) */
    if (frame_len < 0) {
        return frame_len;
    }

    /* Send frame through underlying transport */
    ssize_t sent = mqtt_transport_send(ws->underlying, ws->send_buf.data, ws->send_buf.len);
    if (sent < 0) {
        return sent;
    }

    if ((size_t)sent != ws->send_buf.len) {
        /* Partial send - this shouldn't happen with blocking I/O */
        return -MQTT_ERR_SEND_FAILED;
    }

    /* Return original data length (not frame length) */
    return (ssize_t)len;
}

static ssize_t ws_transport_recv(mqtt_transport_t *transport,
                                  void *buf, size_t len)
{
    mqtt_ws_transport_t *ws = (mqtt_ws_transport_t *)transport;

    if (!ws || !buf || len == 0) {
        return -MQTT_ERR_INVALID_ARG;
    }

    if (ws->base.status != MQTT_TRANSPORT_CONNECTED) {
        return -MQTT_ERR_NOT_CONNECTED;
    }

    /* First, return any buffered payload data */
    if (ws->payload_buf.len > ws->payload_read_pos) {
        size_t available = ws->payload_buf.len - ws->payload_read_pos;
        size_t to_copy = (available < len) ? available : len;
        memcpy(buf, ws->payload_buf.data + ws->payload_read_pos, to_copy);
        ws->payload_read_pos += to_copy;

        /* Reset buffer if fully consumed */
        if (ws->payload_read_pos >= ws->payload_buf.len) {
            mqtt_buffer_reset(&ws->payload_buf);
            ws->payload_read_pos = 0;
        }

        return (ssize_t)to_copy;
    }

    /* Need to receive and process more WebSocket frames */
    while (1) {
        /* Ensure we have buffer space */
        if (mqtt_buffer_reserve(&ws->recv_buf, ws->recv_buf.len + 4096) != MQTT_OK) {
            return -MQTT_ERR_NOMEM;
        }

        /* Receive data from underlying transport */
        ssize_t received = mqtt_transport_recv(ws->underlying,
                                                ws->recv_buf.data + ws->recv_buf.len,
                                                ws->recv_buf.capacity - ws->recv_buf.len);
        if (received <= 0) {
            if (received == 0) {
                ws->base.status = MQTT_TRANSPORT_DISCONNECTED;
            }
            return received;
        }

        ws->recv_buf.len += (size_t)received;

        /* Try to decode frame(s) */
        while (ws->recv_buf.len > 0) {
            ws_frame_header_t header;
            int header_len = ws_frame_decode_header(ws->recv_buf.data, ws->recv_buf.len, &header);

            if (header_len == 0) {
                /* Need more data for header */
                break;
            }

            if (header_len < 0) {
                /* Protocol error */
                ws->base.status = MQTT_TRANSPORT_ERROR;
                return header_len;
            }

            /* Check if we have complete frame */
            size_t frame_total = header.header_len + header.payload_len;
            if (ws->recv_buf.len < frame_total) {
                /* Need more data for payload */
                break;
            }

            /* Extract and unmask payload */
            uint8_t *payload = ws->recv_buf.data + header.header_len;
            if (header.masked) {
                ws_frame_unmask(payload, (size_t)header.payload_len, header.mask_key);
            }

            /* Handle frame based on opcode */
            switch (header.opcode) {
                case WS_OPCODE_BINARY:
                case WS_OPCODE_TEXT:
                case WS_OPCODE_CONTINUATION:
                    /* Data frame - copy to payload buffer */
                    if (mqtt_buffer_reserve(&ws->payload_buf,
                                            ws->payload_buf.len + header.payload_len) != MQTT_OK) {
                        return -MQTT_ERR_NOMEM;
                    }
                    memcpy(ws->payload_buf.data + ws->payload_buf.len,
                           payload, (size_t)header.payload_len);
                    ws->payload_buf.len += (size_t)header.payload_len;
                    break;

                case WS_OPCODE_CLOSE:
                    /* Connection close */
                    ws->base.status = MQTT_TRANSPORT_DISCONNECTED;
                    /* Send close frame in response if we didn't initiate */
                    {
                        mqtt_buffer_t close_buf;
                        mqtt_buffer_init(&close_buf, 128);
                        ws_build_close_frame(&close_buf, WS_CLOSE_NORMAL, NULL);
                        mqtt_transport_send(ws->underlying, close_buf.data, close_buf.len);
                        mqtt_buffer_cleanup(&close_buf);
                    }
                    return 0;  /* Connection closed */

                case WS_OPCODE_PING:
                    /* Respond with pong */
                    {
                        mqtt_buffer_t pong_buf;
                        mqtt_buffer_init(&pong_buf, 128);
                        ws_build_pong_frame(&pong_buf, payload, (size_t)header.payload_len);
                        mqtt_transport_send(ws->underlying, pong_buf.data, pong_buf.len);
                        mqtt_buffer_cleanup(&pong_buf);
                    }
                    break;

                case WS_OPCODE_PONG:
                    /* Ignore pong frames */
                    break;

                default:
                    /* Unknown opcode - protocol error */
                    ws->base.status = MQTT_TRANSPORT_ERROR;
                    return -MQTT_ERR_PROTOCOL;
            }

            /* Remove processed frame from buffer */
            size_t remaining = ws->recv_buf.len - frame_total;
            if (remaining > 0) {
                memmove(ws->recv_buf.data, ws->recv_buf.data + frame_total, remaining);
            }
            ws->recv_buf.len = remaining;
        }

        /* Return data if we have any */
        if (ws->payload_buf.len > ws->payload_read_pos) {
            size_t available = ws->payload_buf.len - ws->payload_read_pos;
            size_t to_copy = (available < len) ? available : len;
            memcpy(buf, ws->payload_buf.data + ws->payload_read_pos, to_copy);
            ws->payload_read_pos += to_copy;

            /* Reset buffer if fully consumed */
            if (ws->payload_read_pos >= ws->payload_buf.len) {
                mqtt_buffer_reset(&ws->payload_buf);
                ws->payload_read_pos = 0;
            }

            return (ssize_t)to_copy;
        }
    }
}

/* ========================================================================== */
/* Utility Functions                                                           */
/* ========================================================================== */

static int ws_transport_get_fd(mqtt_transport_t *transport)
{
    mqtt_ws_transport_t *ws = (mqtt_ws_transport_t *)transport;

    if (!ws || !ws->underlying) {
        return -1;
    }

    return mqtt_transport_get_fd(ws->underlying);
}

static mqtt_error_t ws_transport_set_blocking(mqtt_transport_t *transport, bool blocking)
{
    mqtt_ws_transport_t *ws = (mqtt_ws_transport_t *)transport;

    if (!ws || !ws->underlying) {
        return MQTT_ERR_INVALID_ARG;
    }

    return mqtt_transport_set_blocking(ws->underlying, blocking);
}

static void ws_transport_destroy(mqtt_transport_t *transport)
{
    mqtt_ws_transport_t *ws = (mqtt_ws_transport_t *)transport;

    if (!ws) {
        return;
    }

    /* Disconnect if still connected */
    if (ws->base.status == MQTT_TRANSPORT_CONNECTED) {
        ws_transport_disconnect(transport);
    }

    /* Destroy underlying transport */
    if (ws->underlying) {
        mqtt_transport_destroy(ws->underlying);
    }

    /* Cleanup buffers */
    mqtt_buffer_cleanup(&ws->recv_buf);
    mqtt_buffer_cleanup(&ws->payload_buf);
    mqtt_buffer_cleanup(&ws->send_buf);
    ws_connection_cleanup(&ws->ws_conn);

    /* Free host string */
    mqtt_free(ws->host);

    /* Free transport structure */
    mqtt_free(ws);
}
