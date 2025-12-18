/**
 * @file generate_corpus.c
 * @brief Generate seed corpus for MQTT fuzz testing
 *
 * This generates a set of valid and edge-case MQTT packets to seed the fuzzer.
 * Run this once to populate the corpus directory before fuzzing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CORPUS_DIR "corpus/"

static int file_count = 0;

static void write_corpus_file(const char *prefix, const uint8_t *data, size_t size)
{
    char filename[256];
    snprintf(filename, sizeof(filename), "%s%s_%03d.bin", CORPUS_DIR, prefix, file_count++);

    FILE *f = fopen(filename, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
        printf("Created: %s (%zu bytes)\n", filename, size);
    } else {
        fprintf(stderr, "Failed to create: %s\n", filename);
    }
}

/* ========================================================================== */
/* Variable-length integer test cases (selector = 0)                          */
/* ========================================================================== */

static void generate_varint_corpus(void)
{
    uint8_t buf[16];

    /* 1-byte varints */
    buf[0] = 0;  /* selector */
    buf[1] = 0x00;
    write_corpus_file("varint", buf, 2);

    buf[1] = 0x7F;  /* max 1-byte */
    write_corpus_file("varint", buf, 2);

    /* 2-byte varints */
    buf[1] = 0x80; buf[2] = 0x01;  /* 128 */
    write_corpus_file("varint", buf, 3);

    buf[1] = 0xFF; buf[2] = 0x7F;  /* max 2-byte */
    write_corpus_file("varint", buf, 3);

    /* 3-byte varints */
    buf[1] = 0x80; buf[2] = 0x80; buf[3] = 0x01;  /* 16384 */
    write_corpus_file("varint", buf, 4);

    buf[1] = 0xFF; buf[2] = 0xFF; buf[3] = 0x7F;  /* max 3-byte */
    write_corpus_file("varint", buf, 4);

    /* 4-byte varints */
    buf[1] = 0x80; buf[2] = 0x80; buf[3] = 0x80; buf[4] = 0x01;  /* 2097152 */
    write_corpus_file("varint", buf, 5);

    buf[1] = 0xFF; buf[2] = 0xFF; buf[3] = 0xFF; buf[4] = 0x7F;  /* max valid */
    write_corpus_file("varint", buf, 5);

    /* Invalid: 5 continuation bytes */
    buf[1] = 0x80; buf[2] = 0x80; buf[3] = 0x80; buf[4] = 0x80; buf[5] = 0x01;
    write_corpus_file("varint", buf, 6);

    /* Invalid: overflow */
    buf[1] = 0xFF; buf[2] = 0xFF; buf[3] = 0xFF; buf[4] = 0xFF; buf[5] = 0x0F;
    write_corpus_file("varint", buf, 6);
}

/* ========================================================================== */
/* UTF-8 decode test cases (selector = 1)                                     */
/* ========================================================================== */

static void generate_utf8_decode_corpus(void)
{
    uint8_t buf[256];
    buf[0] = 1;  /* selector */

    /* Empty string */
    buf[1] = 0x00; buf[2] = 0x00;
    write_corpus_file("utf8dec", buf, 3);

    /* Short ASCII string "Hi" */
    buf[1] = 0x00; buf[2] = 0x02;
    buf[3] = 'H'; buf[4] = 'i';
    write_corpus_file("utf8dec", buf, 5);

    /* UTF-8 multi-byte: "日本" */
    buf[1] = 0x00; buf[2] = 0x06;
    buf[3] = 0xE6; buf[4] = 0x97; buf[5] = 0xA5;  /* 日 */
    buf[6] = 0xE6; buf[7] = 0x9C; buf[8] = 0xAC;  /* 本 */
    write_corpus_file("utf8dec", buf, 9);

    /* Max length prefix */
    buf[1] = 0xFF; buf[2] = 0xFF;
    write_corpus_file("utf8dec", buf, 3);

    /* Length exceeds buffer */
    buf[1] = 0x00; buf[2] = 0xFF;
    buf[3] = 'A';
    write_corpus_file("utf8dec", buf, 4);
}

/* ========================================================================== */
/* UTF-8 validate test cases (selector = 2)                                   */
/* ========================================================================== */

static void generate_utf8_validate_corpus(void)
{
    uint8_t buf[256];
    buf[0] = 2;  /* selector */

    /* Valid ASCII */
    strcpy((char *)buf + 1, "Hello, World!");
    write_corpus_file("utf8val", buf, 1 + strlen("Hello, World!"));

    /* Valid 2-byte UTF-8 (é = U+00E9) */
    buf[1] = 0xC3; buf[2] = 0xA9;
    write_corpus_file("utf8val", buf, 3);

    /* Valid 3-byte UTF-8 (€ = U+20AC) */
    buf[1] = 0xE2; buf[2] = 0x82; buf[3] = 0xAC;
    write_corpus_file("utf8val", buf, 4);

    /* Valid 4-byte UTF-8 (🎉 = U+1F389) */
    buf[1] = 0xF0; buf[2] = 0x9F; buf[3] = 0x8E; buf[4] = 0x89;
    write_corpus_file("utf8val", buf, 5);

    /* Invalid: Overlong encoding of ASCII */
    buf[1] = 0xC0; buf[2] = 0x80;
    write_corpus_file("utf8val", buf, 3);

    /* Invalid: Surrogate half (U+D800) */
    buf[1] = 0xED; buf[2] = 0xA0; buf[3] = 0x80;
    write_corpus_file("utf8val", buf, 4);

    /* Invalid: Continuation byte without start */
    buf[1] = 0x80;
    write_corpus_file("utf8val", buf, 2);

    /* Invalid: Truncated sequence */
    buf[1] = 0xE0; buf[2] = 0x80;
    write_corpus_file("utf8val", buf, 3);

    /* Invalid: Null byte (not allowed in MQTT) */
    buf[1] = 0x00;
    write_corpus_file("utf8val", buf, 2);
}

/* ========================================================================== */
/* Fixed header test cases (selector = 3)                                     */
/* ========================================================================== */

static void generate_fixed_header_corpus(void)
{
    uint8_t buf[16];
    buf[0] = 3;  /* selector */

    /* CONNECT packet */
    buf[1] = 0x10;  /* type=1, flags=0 */
    buf[2] = 0x00;  /* remaining length = 0 */
    write_corpus_file("fixhdr", buf, 3);

    /* CONNACK packet */
    buf[1] = 0x20;  /* type=2, flags=0 */
    buf[2] = 0x02;  /* remaining length = 2 */
    write_corpus_file("fixhdr", buf, 3);

    /* PUBLISH QoS 0 */
    buf[1] = 0x30;  /* type=3, flags=0 */
    buf[2] = 0x0A;  /* remaining length = 10 */
    write_corpus_file("fixhdr", buf, 3);

    /* PUBLISH QoS 1 */
    buf[1] = 0x32;  /* type=3, flags=0010 */
    buf[2] = 0x0A;
    write_corpus_file("fixhdr", buf, 3);

    /* PUBLISH QoS 2, DUP, RETAIN */
    buf[1] = 0x3D;  /* type=3, flags=1101 */
    buf[2] = 0x0A;
    write_corpus_file("fixhdr", buf, 3);

    /* SUBSCRIBE */
    buf[1] = 0x82;  /* type=8, flags=0010 */
    buf[2] = 0x05;
    write_corpus_file("fixhdr", buf, 3);

    /* PINGREQ */
    buf[1] = 0xC0;  /* type=12, flags=0 */
    buf[2] = 0x00;
    write_corpus_file("fixhdr", buf, 3);

    /* DISCONNECT */
    buf[1] = 0xE0;  /* type=14, flags=0 */
    buf[2] = 0x00;
    write_corpus_file("fixhdr", buf, 3);

    /* Invalid packet type 0 */
    buf[1] = 0x00;
    buf[2] = 0x00;
    write_corpus_file("fixhdr", buf, 3);

    /* Invalid packet type 15 */
    buf[1] = 0xF0;
    buf[2] = 0x00;
    write_corpus_file("fixhdr", buf, 3);

    /* Large remaining length */
    buf[1] = 0x30;
    buf[2] = 0xFF; buf[3] = 0xFF; buf[4] = 0xFF; buf[5] = 0x7F;
    write_corpus_file("fixhdr", buf, 6);
}

/* ========================================================================== */
/* CONNACK test cases (selector = 4)                                          */
/* ========================================================================== */

static void generate_connack_corpus(void)
{
    uint8_t buf[16];
    buf[0] = 4;  /* selector */

    /* Success, session not present */
    buf[1] = 0x00; buf[2] = 0x00;
    write_corpus_file("connack", buf, 3);

    /* Success, session present */
    buf[1] = 0x01; buf[2] = 0x00;
    write_corpus_file("connack", buf, 3);

    /* Bad return codes */
    for (int i = 0; i <= 6; i++) {
        buf[1] = 0x00; buf[2] = (uint8_t)i;
        write_corpus_file("connack", buf, 3);
    }

    /* Invalid: reserved bits set */
    buf[1] = 0xFE; buf[2] = 0x00;
    write_corpus_file("connack", buf, 3);

    /* Empty (too short) */
    write_corpus_file("connack", buf, 1);

    /* One byte only */
    buf[1] = 0x00;
    write_corpus_file("connack", buf, 2);
}

/* ========================================================================== */
/* PUBLISH test cases (selector = 5)                                          */
/* ========================================================================== */

static void generate_publish_corpus(void)
{
    uint8_t buf[256];
    buf[0] = 5;  /* selector - first byte will be used as flags */

    /* QoS 0: flags in buf[1], then topic length, topic, payload */
    buf[1] = 0x00;  /* flags: QoS 0, no DUP, no RETAIN */
    buf[2] = 0x00; buf[3] = 0x04;  /* topic length = 4 */
    buf[4] = 't'; buf[5] = 'e'; buf[6] = 's'; buf[7] = 't';  /* topic */
    buf[8] = 'H'; buf[9] = 'i';  /* payload */
    write_corpus_file("publish", buf, 10);

    /* QoS 1 with packet ID */
    buf[1] = 0x02;  /* flags: QoS 1 */
    buf[2] = 0x00; buf[3] = 0x04;
    buf[4] = 't'; buf[5] = 'e'; buf[6] = 's'; buf[7] = 't';
    buf[8] = 0x00; buf[9] = 0x01;  /* packet ID = 1 */
    buf[10] = 'H'; buf[11] = 'i';
    write_corpus_file("publish", buf, 12);

    /* QoS 2 with DUP and RETAIN */
    buf[1] = 0x0D;  /* flags: DUP=1, QoS 2, RETAIN=1 */
    write_corpus_file("publish", buf, 12);

    /* Empty topic */
    buf[1] = 0x00;
    buf[2] = 0x00; buf[3] = 0x00;
    write_corpus_file("publish", buf, 4);

    /* Topic with wildcard (invalid) */
    buf[2] = 0x00; buf[3] = 0x05;
    buf[4] = 't'; buf[5] = 'e'; buf[6] = 's'; buf[7] = 't'; buf[8] = '#';
    write_corpus_file("publish", buf, 9);

    buf[8] = '+';
    write_corpus_file("publish", buf, 9);

    /* Invalid QoS 3 */
    buf[1] = 0x06;  /* flags: QoS 3 (invalid) */
    buf[2] = 0x00; buf[3] = 0x04;
    buf[4] = 't'; buf[5] = 'e'; buf[6] = 's'; buf[7] = 't';
    write_corpus_file("publish", buf, 8);
}

/* ========================================================================== */
/* Full packet test cases (selector = 7)                                      */
/* ========================================================================== */

static void generate_full_packet_corpus(void)
{
    uint8_t buf[256];
    buf[0] = 7;  /* selector */

    /* Complete CONNACK */
    buf[1] = 0x20;  /* CONNACK */
    buf[2] = 0x02;  /* remaining length */
    buf[3] = 0x00;  /* flags */
    buf[4] = 0x00;  /* return code: success */
    write_corpus_file("fullpkt", buf, 5);

    /* Complete PINGRESP */
    buf[1] = 0xD0;  /* PINGRESP */
    buf[2] = 0x00;  /* remaining length */
    write_corpus_file("fullpkt", buf, 3);

    /* Complete PUBLISH QoS 0 */
    buf[1] = 0x30;  /* PUBLISH */
    buf[2] = 0x09;  /* remaining length */
    buf[3] = 0x00; buf[4] = 0x05;  /* topic length */
    buf[5] = 'h'; buf[6] = 'e'; buf[7] = 'l'; buf[8] = 'l'; buf[9] = 'o';
    buf[10] = 'H'; buf[11] = 'i';  /* payload */
    write_corpus_file("fullpkt", buf, 12);

    /* Complete PUBACK */
    buf[1] = 0x40;  /* PUBACK */
    buf[2] = 0x02;  /* remaining length */
    buf[3] = 0x00; buf[4] = 0x01;  /* packet ID = 1 */
    write_corpus_file("fullpkt", buf, 5);

    /* Complete SUBACK */
    buf[1] = 0x90;  /* SUBACK */
    buf[2] = 0x03;  /* remaining length */
    buf[3] = 0x00; buf[4] = 0x01;  /* packet ID = 1 */
    buf[5] = 0x00;  /* granted QoS = 0 */
    write_corpus_file("fullpkt", buf, 6);

    /* Truncated packet */
    buf[1] = 0x30;  /* PUBLISH */
    buf[2] = 0xFF;  /* claims 127 bytes but we don't provide them */
    write_corpus_file("fullpkt", buf, 3);
}

int main(void)
{
    printf("Generating MQTT fuzz corpus...\n\n");

    generate_varint_corpus();
    generate_utf8_decode_corpus();
    generate_utf8_validate_corpus();
    generate_fixed_header_corpus();
    generate_connack_corpus();
    generate_publish_corpus();
    generate_full_packet_corpus();

    printf("\nGenerated %d corpus files\n", file_count);
    return 0;
}
