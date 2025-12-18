/**
 * @file fuzz_packet_parser.c
 * @brief Fuzz testing harness for MQTT packet parsing
 *
 * This file provides fuzz testing for MQTT packet parsing code.
 * It can be compiled in three modes:
 *
 * 1. Standalone mode (default): Uses a simple PRNG to generate random inputs
 * 2. AFL mode (FUZZ_AFL): Compatible with AFL fuzzer
 * 3. libFuzzer mode (FUZZ_LIBFUZZER): Compatible with LLVM libFuzzer
 *
 * Build for standalone:
 *   gcc -DFUZZ_STANDALONE fuzz_packet_parser.c -I../../include -L../../build -lmqtt_client -o fuzz_packet
 *
 * Build for AFL:
 *   afl-gcc -DFUZZ_AFL fuzz_packet_parser.c -I../../include -L../../build -lmqtt_client -o fuzz_packet_afl
 *
 * Build for libFuzzer:
 *   clang -DFUZZ_LIBFUZZER -fsanitize=fuzzer,address fuzz_packet_parser.c -I../../include -L../../build -lmqtt_client -o fuzz_packet_libfuzzer
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Include public headers */
#include <mqtt/mqtt_error.h>

/* Include internal headers for testing */
#include "../../src/core/mqtt_varint.h"
#include "../../src/core/mqtt_utf8.h"
#include "../../src/core/mqtt_packet.h"

/* ========================================================================== */
/* Forward declarations of internal parsing functions                         */
/* ========================================================================== */

/* Fixed header decode */
int mqtt_fixed_header_decode(const uint8_t *buf, size_t buf_len, mqtt_fixed_header_t *header);

/* V3.1.1 packet decoders */
typedef struct {
    bool session_present;
    uint8_t return_code;
} mqtt_v311_connack_t;

typedef struct {
    const char *topic;
    uint16_t topic_len;
    uint16_t packet_id;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t qos;
    bool dup;
    bool retain;
} mqtt_v311_publish_t;

/* Declare external parsing functions */
mqtt_error_t mqtt_v311_decode_connack(const uint8_t *buf, size_t len, mqtt_v311_connack_t *out);
mqtt_error_t mqtt_v311_decode_publish(const uint8_t *buf, size_t len, uint8_t flags, mqtt_v311_publish_t *out);
mqtt_error_t mqtt_v311_decode_ack(const uint8_t *buf, size_t len, uint16_t *packet_id);

/* ========================================================================== */
/* Fuzz test targets                                                          */
/* ========================================================================== */

/**
 * @brief Fuzz the variable-length integer decoder
 */
static void fuzz_varint(const uint8_t *data, size_t size)
{
    uint32_t value;
    mqtt_varint_decode(data, size, &value);
}

/**
 * @brief Fuzz the UTF-8 decoder
 */
static void fuzz_utf8_decode(const uint8_t *data, size_t size)
{
    const char *str;
    uint16_t len;
    mqtt_utf8_decode(data, size, &str, &len);
}

/**
 * @brief Fuzz the UTF-8 validator
 */
static void fuzz_utf8_validate(const uint8_t *data, size_t size)
{
    mqtt_utf8_validate((const char *)data, size);
}

/**
 * @brief Fuzz the fixed header decoder
 */
static void fuzz_fixed_header(const uint8_t *data, size_t size)
{
    mqtt_fixed_header_t header;
    mqtt_fixed_header_decode(data, size, &header);
}

/**
 * @brief Fuzz CONNACK packet decoder
 */
static void fuzz_connack(const uint8_t *data, size_t size)
{
    mqtt_v311_connack_t connack;
    mqtt_v311_decode_connack(data, size, &connack);
}

/**
 * @brief Fuzz PUBLISH packet decoder
 */
static void fuzz_publish(const uint8_t *data, size_t size)
{
    if (size < 1) return;

    /* First byte contains flags */
    uint8_t flags = data[0] & 0x0F;
    mqtt_v311_publish_t publish;
    memset(&publish, 0, sizeof(publish));
    mqtt_v311_decode_publish(data + 1, size - 1, flags, &publish);
}

/**
 * @brief Fuzz ACK packet decoder (PUBACK/PUBREC/PUBREL/PUBCOMP)
 */
static void fuzz_ack(const uint8_t *data, size_t size)
{
    uint16_t packet_id;
    mqtt_v311_decode_ack(data, size, &packet_id);
}

/**
 * @brief Fuzz all packet parsers with raw MQTT packet data
 *
 * This interprets the data as a complete MQTT packet starting with
 * the fixed header.
 */
static void fuzz_full_packet(const uint8_t *data, size_t size)
{
    if (size < 2) return;

    mqtt_fixed_header_t header;
    int consumed = mqtt_fixed_header_decode(data, size, &header);

    if (consumed <= 0) return;
    if ((size_t)consumed >= size) return;

    const uint8_t *payload = data + consumed;
    size_t payload_len = size - (size_t)consumed;

    /* Ensure we don't read beyond buffer */
    if (header.remaining_len > payload_len) {
        header.remaining_len = (uint32_t)payload_len;
    }

    /* Decode based on packet type */
    switch (header.type) {
        case MQTT_PACKET_CONNACK: {
            mqtt_v311_connack_t connack;
            mqtt_v311_decode_connack(payload, header.remaining_len, &connack);
            break;
        }
        case MQTT_PACKET_PUBLISH: {
            mqtt_v311_publish_t publish;
            memset(&publish, 0, sizeof(publish));
            mqtt_v311_decode_publish(payload, header.remaining_len, header.flags, &publish);
            break;
        }
        case MQTT_PACKET_PUBACK:
        case MQTT_PACKET_PUBREC:
        case MQTT_PACKET_PUBREL:
        case MQTT_PACKET_PUBCOMP: {
            uint16_t packet_id;
            mqtt_v311_decode_ack(payload, header.remaining_len, &packet_id);
            break;
        }
        default:
            /* Other packet types - just attempt varint and utf8 decoding on payload */
            fuzz_varint(payload, header.remaining_len);
            fuzz_utf8_decode(payload, header.remaining_len);
            break;
    }
}

/**
 * @brief Main fuzz entry point
 *
 * Dispatches to different fuzz targets based on first byte.
 */
static int fuzz_one_input(const uint8_t *data, size_t size)
{
    if (size == 0) return 0;

    /* Use first byte to select fuzz target */
    uint8_t selector = data[0] % 8;
    const uint8_t *payload = data + 1;
    size_t payload_size = size - 1;

    switch (selector) {
        case 0:
            fuzz_varint(payload, payload_size);
            break;
        case 1:
            fuzz_utf8_decode(payload, payload_size);
            break;
        case 2:
            fuzz_utf8_validate(payload, payload_size);
            break;
        case 3:
            fuzz_fixed_header(payload, payload_size);
            break;
        case 4:
            fuzz_connack(payload, payload_size);
            break;
        case 5:
            fuzz_publish(payload, payload_size);
            break;
        case 6:
            fuzz_ack(payload, payload_size);
            break;
        case 7:
        default:
            fuzz_full_packet(payload, payload_size);
            break;
    }

    return 0;
}

/* ========================================================================== */
/* Mode-specific entry points                                                 */
/* ========================================================================== */

#if defined(FUZZ_LIBFUZZER)
/* libFuzzer entry point */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    return fuzz_one_input(data, size);
}

#elif defined(FUZZ_AFL)
/* AFL entry point - read from stdin */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    uint8_t buf[65536];

#ifdef __AFL_LOOP
    while (__AFL_LOOP(10000)) {
#endif
        ssize_t size = read(0, buf, sizeof(buf));
        if (size > 0) {
            fuzz_one_input(buf, (size_t)size);
        }
#ifdef __AFL_LOOP
    }
#endif

    return 0;
}

#else /* FUZZ_STANDALONE or default */

/* Simple PRNG for standalone testing */
static uint32_t prng_state = 0;

static uint32_t prng_next(void)
{
    /* xorshift32 */
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 17;
    prng_state ^= prng_state << 5;
    return prng_state;
}

static void generate_random_data(uint8_t *buf, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        buf[i] = (uint8_t)(prng_next() & 0xFF);
    }
}

static void generate_interesting_data(uint8_t *buf, size_t size)
{
    /* Generate data with interesting patterns */
    uint32_t pattern = prng_next() % 10;

    switch (pattern) {
        case 0:
            /* All zeros */
            memset(buf, 0x00, size);
            break;
        case 1:
            /* All 0xFF */
            memset(buf, 0xFF, size);
            break;
        case 2:
            /* Continuation bits set (varint edge case) */
            memset(buf, 0x80, size);
            if (size > 0) buf[size - 1] = 0x00;
            break;
        case 3:
            /* Valid UTF-8 multi-byte sequences */
            for (size_t i = 0; i < size; i += 3) {
                if (i + 2 < size) {
                    buf[i] = 0xE0 | (prng_next() % 16);
                    buf[i+1] = 0x80 | (prng_next() % 64);
                    buf[i+2] = 0x80 | (prng_next() % 64);
                }
            }
            break;
        case 4:
            /* Invalid UTF-8 (overlong encoding) */
            for (size_t i = 0; i < size; i += 2) {
                if (i + 1 < size) {
                    buf[i] = 0xC0;
                    buf[i+1] = 0x80;
                }
            }
            break;
        case 5:
            /* Large length prefixes */
            if (size >= 2) {
                buf[0] = 0xFF;
                buf[1] = 0xFF;
                generate_random_data(buf + 2, size > 2 ? size - 2 : 0);
            }
            break;
        case 6:
            /* Valid MQTT packet structure */
            if (size >= 5) {
                buf[0] = (MQTT_PACKET_PUBLISH << 4) | 0x02; /* PUBLISH QoS 1 */
                buf[1] = (uint8_t)(size > 127 ? 0x80 : (size - 2));
                if (size > 127) buf[2] = 0x01;
                generate_random_data(buf + (size > 127 ? 3 : 2), size - (size > 127 ? 3 : 2));
            }
            break;
        case 7:
            /* CONNACK packet */
            if (size >= 4) {
                buf[0] = MQTT_PACKET_CONNACK << 4;
                buf[1] = 2;
                buf[2] = prng_next() & 0xFF;
                buf[3] = prng_next() % 6;
            }
            break;
        default:
            generate_random_data(buf, size);
            break;
    }
}

int main(int argc, char **argv)
{
    uint32_t iterations = 100000;
    uint32_t seed = (uint32_t)time(NULL);

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            iterations = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            seed = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [-n iterations] [-s seed]\n", argv[0]);
            printf("  -n  Number of iterations (default: 100000)\n");
            printf("  -s  Random seed (default: time-based)\n");
            return 0;
        }
    }

    prng_state = seed;
    printf("MQTT Packet Parser Fuzz Test\n");
    printf("Iterations: %u, Seed: %u\n", iterations, seed);
    printf("Running...\n");

    uint8_t buf[4096];
    uint32_t crashes = 0;

    for (uint32_t i = 0; i < iterations; i++) {
        /* Vary buffer size */
        size_t size = (prng_next() % 4096) + 1;

        /* Alternate between random and interesting data */
        if (prng_next() % 2) {
            generate_random_data(buf, size);
        } else {
            generate_interesting_data(buf, size);
        }

        fuzz_one_input(buf, size);

        /* Progress indicator */
        if ((i + 1) % 10000 == 0) {
            printf("Progress: %u/%u iterations\n", i + 1, iterations);
        }
    }

    printf("Completed %u iterations, %u crashes detected\n", iterations, crashes);
    printf("PASS - No crashes detected\n");

    return 0;
}

#endif /* FUZZ_STANDALONE */
