/**
 * @file mqtt_websocket.c
 * @brief WebSocket Transport Implementation
 *
 * Implements WebSocket framing and HTTP upgrade handshake for MQTT transport.
 */

#define _POSIX_C_SOURCE 200809L

#include "mqtt_websocket.h"
#include "../../memory/mqtt_memory.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* WebSocket GUID for Sec-WebSocket-Accept calculation */
static const char *WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* ========================================================================== */
/* SHA-1 Implementation (for Sec-WebSocket-Accept)                            */
/* ========================================================================== */

/* Simple SHA-1 implementation for WebSocket handshake */
typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} sha1_context_t;

static void sha1_init(sha1_context_t *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = ctx->count[1] = 0;
}

#define ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

static void sha1_transform(uint32_t state[5], const uint8_t buffer[64])
{
    uint32_t a, b, c, d, e, w[80];

    /* Copy buffer to 16 32-bit words */
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)buffer[i * 4] << 24) |
               ((uint32_t)buffer[i * 4 + 1] << 16) |
               ((uint32_t)buffer[i * 4 + 2] << 8) |
               ((uint32_t)buffer[i * 4 + 3]);
    }

    /* Extend to 80 words */
    for (int i = 16; i < 80; i++) {
        w[i] = ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }

        uint32_t temp = ROL(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROL(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void sha1_update(sha1_context_t *ctx, const uint8_t *data, size_t len)
{
    uint32_t i, j;

    j = (ctx->count[0] >> 3) & 63;
    if ((ctx->count[0] += (uint32_t)(len << 3)) < (len << 3)) {
        ctx->count[1]++;
    }
    ctx->count[1] += (uint32_t)(len >> 29);

    if ((j + len) > 63) {
        memcpy(&ctx->buffer[j], data, (i = 64 - j));
        sha1_transform(ctx->state, ctx->buffer);
        for (; i + 63 < len; i += 64) {
            sha1_transform(ctx->state, &data[i]);
        }
        j = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[j], &data[i], len - i);
}

static void sha1_final(sha1_context_t *ctx, uint8_t digest[20])
{
    uint8_t finalcount[8];

    for (int i = 0; i < 8; i++) {
        finalcount[i] = (uint8_t)((ctx->count[(i >= 4 ? 0 : 1)] >>
                                   ((3 - (i & 3)) * 8)) & 255);
    }

    uint8_t c = 0x80;
    sha1_update(ctx, &c, 1);
    while ((ctx->count[0] & 504) != 448) {
        c = 0x00;
        sha1_update(ctx, &c, 1);
    }
    sha1_update(ctx, finalcount, 8);

    for (int i = 0; i < 20; i++) {
        digest[i] = (uint8_t)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}

/* ========================================================================== */
/* Base64 Encoding                                                             */
/* ========================================================================== */

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const uint8_t *data, size_t len, char *output)
{
    size_t out_len = 0;

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = ((uint32_t)data[i]) << 16;
        if (i + 1 < len) n |= ((uint32_t)data[i + 1]) << 8;
        if (i + 2 < len) n |= data[i + 2];

        output[out_len++] = base64_chars[(n >> 18) & 0x3F];
        output[out_len++] = base64_chars[(n >> 12) & 0x3F];
        output[out_len++] = (i + 1 < len) ? base64_chars[(n >> 6) & 0x3F] : '=';
        output[out_len++] = (i + 2 < len) ? base64_chars[n & 0x3F] : '=';
    }

    output[out_len] = '\0';
    return out_len;
}

/* ========================================================================== */
/* Frame Size Calculation                                                      */
/* ========================================================================== */

size_t ws_frame_size(size_t payload_len, bool masked)
{
    size_t header_size = 2;  /* First two bytes always present */

    if (payload_len >= 126 && payload_len <= 65535) {
        header_size += 2;  /* 16-bit extended length */
    } else if (payload_len > 65535) {
        header_size += 8;  /* 64-bit extended length */
    }

    if (masked) {
        header_size += 4;  /* Masking key */
    }

    return header_size + payload_len;
}

/* ========================================================================== */
/* Frame Encoding                                                              */
/* ========================================================================== */

ssize_t ws_frame_encode(mqtt_buffer_t *buf, ws_opcode_t opcode,
                        const uint8_t *payload, size_t payload_len,
                        bool fin, bool masked)
{
    size_t frame_size = ws_frame_size(payload_len, masked);

    if (mqtt_buffer_reserve(buf, buf->len + frame_size) != MQTT_OK) {
        return -MQTT_ERR_NOMEM;
    }

    uint8_t *out = buf->data + buf->len;
    size_t pos = 0;

    /* First byte: FIN + RSV + opcode */
    out[pos++] = (fin ? 0x80 : 0x00) | (opcode & 0x0F);

    /* Second byte: MASK + payload length */
    uint8_t mask_bit = masked ? 0x80 : 0x00;

    if (payload_len < 126) {
        out[pos++] = mask_bit | (uint8_t)payload_len;
    } else if (payload_len <= 65535) {
        out[pos++] = mask_bit | 126;
        out[pos++] = (payload_len >> 8) & 0xFF;
        out[pos++] = payload_len & 0xFF;
    } else {
        out[pos++] = mask_bit | 127;
        out[pos++] = 0;
        out[pos++] = 0;
        out[pos++] = 0;
        out[pos++] = 0;
        out[pos++] = (payload_len >> 24) & 0xFF;
        out[pos++] = (payload_len >> 16) & 0xFF;
        out[pos++] = (payload_len >> 8) & 0xFF;
        out[pos++] = payload_len & 0xFF;
    }

    /* Masking key (if masked) */
    uint8_t mask_key[4] = {0};
    if (masked) {
        /* Generate random mask key */
        uint32_t r = (uint32_t)rand();
        mask_key[0] = (r >> 24) & 0xFF;
        mask_key[1] = (r >> 16) & 0xFF;
        mask_key[2] = (r >> 8) & 0xFF;
        mask_key[3] = r & 0xFF;

        out[pos++] = mask_key[0];
        out[pos++] = mask_key[1];
        out[pos++] = mask_key[2];
        out[pos++] = mask_key[3];
    }

    /* Payload (masked if required) */
    if (payload && payload_len > 0) {
        if (masked) {
            for (size_t i = 0; i < payload_len; i++) {
                out[pos++] = payload[i] ^ mask_key[i & 3];
            }
        } else {
            memcpy(&out[pos], payload, payload_len);
            pos += payload_len;
        }
    }

    buf->len += pos;
    return (ssize_t)pos;
}

/* ========================================================================== */
/* Frame Decoding                                                              */
/* ========================================================================== */

int ws_frame_decode_header(const uint8_t *data, size_t len, ws_frame_header_t *header)
{
    if (len < 2) {
        return 0;  /* Need more data */
    }

    memset(header, 0, sizeof(*header));

    /* First byte */
    header->fin = (data[0] & 0x80) != 0;
    header->rsv1 = (data[0] & 0x40) != 0;
    header->rsv2 = (data[0] & 0x20) != 0;
    header->rsv3 = (data[0] & 0x10) != 0;
    header->opcode = (ws_opcode_t)(data[0] & 0x0F);

    /* Check reserved bits */
    if (header->rsv1 || header->rsv2 || header->rsv3) {
        return -MQTT_ERR_PROTOCOL;  /* Reserved bits must be 0 */
    }

    /* Second byte */
    header->masked = (data[1] & 0x80) != 0;
    uint8_t payload_len7 = data[1] & 0x7F;

    size_t header_len = 2;

    /* Extended payload length */
    if (payload_len7 == 126) {
        if (len < 4) return 0;
        header->payload_len = ((uint64_t)data[2] << 8) | data[3];
        header_len = 4;
    } else if (payload_len7 == 127) {
        if (len < 10) return 0;
        header->payload_len = ((uint64_t)data[2] << 56) |
                              ((uint64_t)data[3] << 48) |
                              ((uint64_t)data[4] << 40) |
                              ((uint64_t)data[5] << 32) |
                              ((uint64_t)data[6] << 24) |
                              ((uint64_t)data[7] << 16) |
                              ((uint64_t)data[8] << 8) |
                              data[9];
        header_len = 10;
    } else {
        header->payload_len = payload_len7;
    }

    /* Masking key */
    if (header->masked) {
        if (len < header_len + 4) return 0;
        memcpy(header->mask_key, &data[header_len], 4);
        header_len += 4;
    }

    header->header_len = header_len;
    return (int)header_len;
}

void ws_frame_unmask(uint8_t *data, size_t len, const uint8_t *mask_key)
{
    for (size_t i = 0; i < len; i++) {
        data[i] ^= mask_key[i & 3];
    }
}

/* ========================================================================== */
/* WebSocket Key Generation                                                    */
/* ========================================================================== */

void ws_generate_key(char *key)
{
    uint8_t random_bytes[16];

    /* Generate 16 random bytes */
    srand((unsigned int)time(NULL) ^ (unsigned int)rand());
    for (int i = 0; i < 16; i++) {
        random_bytes[i] = (uint8_t)(rand() & 0xFF);
    }

    /* Base64 encode to get 24-character key */
    base64_encode(random_bytes, 16, key);
}

void ws_calculate_accept(const char *key, char *accept)
{
    sha1_context_t sha1;
    uint8_t digest[20];

    /* SHA-1(key + GUID) */
    sha1_init(&sha1);
    sha1_update(&sha1, (const uint8_t *)key, strlen(key));
    sha1_update(&sha1, (const uint8_t *)WS_GUID, strlen(WS_GUID));
    sha1_final(&sha1, digest);

    /* Base64 encode the digest */
    base64_encode(digest, 20, accept);
}

/* ========================================================================== */
/* HTTP Upgrade Handshake                                                      */
/* ========================================================================== */

ssize_t ws_build_upgrade_request(mqtt_buffer_t *buf, const char *host,
                                  const char *path, const char *key,
                                  const char *subprotocol,
                                  const char **extra_headers)
{
    /* Estimate required size */
    size_t needed = 512;  /* Base headers */
    if (extra_headers) {
        for (const char **h = extra_headers; *h; h++) {
            needed += strlen(*h) + 4;
        }
    }

    if (mqtt_buffer_reserve(buf, buf->len + needed) != MQTT_OK) {
        return -MQTT_ERR_NOMEM;
    }

    /* Build request */
    char *out = (char *)buf->data + buf->len;
    size_t pos = 0;

    pos += (size_t)sprintf(out + pos, "GET %s HTTP/1.1\r\n", path ? path : "/mqtt");
    pos += (size_t)sprintf(out + pos, "Host: %s\r\n", host);
    pos += (size_t)sprintf(out + pos, "Upgrade: websocket\r\n");
    pos += (size_t)sprintf(out + pos, "Connection: Upgrade\r\n");
    pos += (size_t)sprintf(out + pos, "Sec-WebSocket-Key: %s\r\n", key);
    pos += (size_t)sprintf(out + pos, "Sec-WebSocket-Version: 13\r\n");

    if (subprotocol) {
        pos += (size_t)sprintf(out + pos, "Sec-WebSocket-Protocol: %s\r\n", subprotocol);
    }

    /* Add extra headers */
    if (extra_headers) {
        for (const char **h = extra_headers; *h; h++) {
            pos += (size_t)sprintf(out + pos, "%s\r\n", *h);
        }
    }

    pos += (size_t)sprintf(out + pos, "\r\n");

    buf->len += pos;
    return (ssize_t)pos;
}

int ws_parse_upgrade_response(const uint8_t *data, size_t len,
                               const char *expected_accept,
                               char *subprotocol, size_t subprotocol_len)
{
    /* Look for end of headers */
    const char *headers = (const char *)data;
    const char *end = strstr(headers, "\r\n\r\n");
    if (!end) {
        return 0;  /* Need more data */
    }

    size_t headers_len = (size_t)(end - headers) + 4;

    /* Check status line */
    if (len < 12 || strncmp(headers, "HTTP/1.1 101", 12) != 0) {
        return -MQTT_ERR_PROTOCOL;
    }

    /* Find Sec-WebSocket-Accept header */
    const char *accept_start = strstr(headers, "Sec-WebSocket-Accept:");
    if (!accept_start || accept_start > end) {
        accept_start = strstr(headers, "sec-websocket-accept:");
    }
    if (!accept_start || accept_start > end) {
        return -MQTT_ERR_PROTOCOL;
    }

    accept_start += 21;  /* Skip header name */
    while (*accept_start == ' ' || *accept_start == '\t') accept_start++;

    /* Extract accept value */
    const char *accept_end = accept_start;
    while (*accept_end != '\r' && *accept_end != '\n' && accept_end < end) {
        accept_end++;
    }

    char accept_value[64];
    size_t accept_len = (size_t)(accept_end - accept_start);
    if (accept_len >= sizeof(accept_value)) {
        return -MQTT_ERR_PROTOCOL;
    }
    memcpy(accept_value, accept_start, accept_len);
    accept_value[accept_len] = '\0';

    /* Validate accept value */
    if (strcmp(accept_value, expected_accept) != 0) {
        return -MQTT_ERR_PROTOCOL;
    }

    /* Find Sec-WebSocket-Protocol header (optional) */
    if (subprotocol && subprotocol_len > 0) {
        subprotocol[0] = '\0';
        const char *proto_start = strstr(headers, "Sec-WebSocket-Protocol:");
        if (!proto_start || proto_start > end) {
            proto_start = strstr(headers, "sec-websocket-protocol:");
        }
        if (proto_start && proto_start < end) {
            proto_start += 23;
            while (*proto_start == ' ' || *proto_start == '\t') proto_start++;

            const char *proto_end = proto_start;
            while (*proto_end != '\r' && *proto_end != '\n' && proto_end < end) {
                proto_end++;
            }

            size_t proto_len = (size_t)(proto_end - proto_start);
            if (proto_len < subprotocol_len) {
                memcpy(subprotocol, proto_start, proto_len);
                subprotocol[proto_len] = '\0';
            }
        }
    }

    return (int)headers_len;
}

/* ========================================================================== */
/* Connection Management                                                       */
/* ========================================================================== */

mqtt_error_t ws_connection_init(ws_connection_t *conn, const char *path,
                                 const char *host, const char *subprotocol,
                                 const char **extra_headers)
{
    memset(conn, 0, sizeof(*conn));

    conn->state = WS_STATE_DISCONNECTED;
    conn->path = path ? path : "/mqtt";
    conn->host = host;
    conn->subprotocol = subprotocol;
    conn->extra_headers = extra_headers;

    /* Generate WebSocket key */
    ws_generate_key(conn->sec_key);
    ws_calculate_accept(conn->sec_key, conn->expected_accept);

    /* Initialize frame buffer */
    mqtt_buffer_init(&conn->frame_buf, 4096);

    return MQTT_OK;
}

void ws_connection_cleanup(ws_connection_t *conn)
{
    mqtt_buffer_cleanup(&conn->frame_buf);
    memset(conn, 0, sizeof(*conn));
}

/* ========================================================================== */
/* Control Frame Helpers                                                       */
/* ========================================================================== */

ssize_t ws_build_close_frame(mqtt_buffer_t *buf, ws_close_code_t code, const char *reason)
{
    uint8_t payload[125];  /* Max control frame payload */
    size_t payload_len = 0;

    /* Status code (big-endian) */
    payload[0] = (code >> 8) & 0xFF;
    payload[1] = code & 0xFF;
    payload_len = 2;

    /* Optional reason string */
    if (reason) {
        size_t reason_len = strlen(reason);
        if (reason_len > 123) reason_len = 123;
        memcpy(&payload[2], reason, reason_len);
        payload_len += reason_len;
    }

    return ws_frame_encode(buf, WS_OPCODE_CLOSE, payload, payload_len, true, true);
}

ssize_t ws_build_ping_frame(mqtt_buffer_t *buf, const uint8_t *data, size_t len)
{
    if (len > 125) len = 125;  /* Max control frame payload */
    return ws_frame_encode(buf, WS_OPCODE_PING, data, len, true, true);
}

ssize_t ws_build_pong_frame(mqtt_buffer_t *buf, const uint8_t *data, size_t len)
{
    if (len > 125) len = 125;  /* Max control frame payload */
    return ws_frame_encode(buf, WS_OPCODE_PONG, data, len, true, true);
}
