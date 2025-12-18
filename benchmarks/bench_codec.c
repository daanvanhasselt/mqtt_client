/**
 * @file bench_codec.c
 * @brief MQTT packet codec benchmark
 *
 * Measures encoding/decoding performance of MQTT variable-length integers
 * and other commonly used codec operations.
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

/* Internal headers */
#include "../src/core/mqtt_varint.h"
#include "../src/core/mqtt_utf8.h"

#define ITERATIONS      10000000

/*******************************************************************************
 * Timing Helpers
 ******************************************************************************/

static double get_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/*******************************************************************************
 * Varint Benchmarks
 ******************************************************************************/

static void bench_varint_encode_1byte(void)
{
    uint8_t buf[4];
    volatile int result = 0;

    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        result += mqtt_varint_encode(100, buf);  /* 1-byte value */
    }

    double elapsed = get_time_sec() - start;
    (void)result;

    printf("Varint encode (1-byte):   %d iterations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS, elapsed, ITERATIONS / elapsed);
}

static void bench_varint_encode_2byte(void)
{
    uint8_t buf[4];
    volatile int result = 0;

    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        result += mqtt_varint_encode(10000, buf);  /* 2-byte value */
    }

    double elapsed = get_time_sec() - start;
    (void)result;

    printf("Varint encode (2-byte):   %d iterations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS, elapsed, ITERATIONS / elapsed);
}

static void bench_varint_encode_4byte(void)
{
    uint8_t buf[4];
    volatile int result = 0;

    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        result += mqtt_varint_encode(100000000, buf);  /* 4-byte value */
    }

    double elapsed = get_time_sec() - start;
    (void)result;

    printf("Varint encode (4-byte):   %d iterations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS, elapsed, ITERATIONS / elapsed);
}

static void bench_varint_decode_1byte(void)
{
    uint8_t buf[4] = {100, 0, 0, 0};  /* 1-byte encoding of 100 */
    uint32_t value;
    volatile int result = 0;

    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        result += mqtt_varint_decode(buf, 4, &value);
    }

    double elapsed = get_time_sec() - start;
    (void)result;

    printf("Varint decode (1-byte):   %d iterations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS, elapsed, ITERATIONS / elapsed);
}

static void bench_varint_decode_2byte(void)
{
    uint8_t buf[4] = {0x90, 0x4E, 0, 0};  /* 2-byte encoding of 10000 */
    uint32_t value;
    volatile int result = 0;

    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        result += mqtt_varint_decode(buf, 4, &value);
    }

    double elapsed = get_time_sec() - start;
    (void)result;

    printf("Varint decode (2-byte):   %d iterations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS, elapsed, ITERATIONS / elapsed);
}

static void bench_varint_decode_4byte(void)
{
    /* 4-byte encoding of 100000000 */
    uint8_t buf[4] = {0x80, 0x94, 0xEB, 0x2F};
    uint32_t value;
    volatile int result = 0;

    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        result += mqtt_varint_decode(buf, 4, &value);
    }

    double elapsed = get_time_sec() - start;
    (void)result;

    printf("Varint decode (4-byte):   %d iterations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS, elapsed, ITERATIONS / elapsed);
}

static void bench_varint_size(void)
{
    volatile int result = 0;

    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        result += mqtt_varint_size(100);
        result += mqtt_varint_size(10000);
        result += mqtt_varint_size(1000000);
        result += mqtt_varint_size(100000000);
    }

    double elapsed = get_time_sec() - start;
    (void)result;

    printf("Varint size (4 calls):    %d iterations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS * 4, elapsed, (ITERATIONS * 4) / elapsed);
}

/*******************************************************************************
 * UTF-8 Benchmarks
 ******************************************************************************/

static void bench_utf8_encode(void)
{
    const char *test_str = "test/topic";
    size_t str_len = 10;
    uint8_t buf[32];
    volatile int result = 0;

    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        result += mqtt_utf8_encode(test_str, str_len, buf, sizeof(buf));
    }

    double elapsed = get_time_sec() - start;
    (void)result;

    printf("UTF-8 encode (10 chars):  %d iterations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS, elapsed, ITERATIONS / elapsed);
}

static void bench_utf8_decode(void)
{
    /* Pre-encoded UTF-8 string: length prefix (0x00, 0x0A) + "test/topic" */
    uint8_t buf[32] = {0x00, 0x0A, 't', 'e', 's', 't', '/', 't', 'o', 'p', 'i', 'c'};
    const char *str;
    uint16_t len;
    volatile int result = 0;

    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        result += mqtt_utf8_decode(buf, sizeof(buf), &str, &len);
    }

    double elapsed = get_time_sec() - start;
    (void)result;

    printf("UTF-8 decode (10 chars):  %d iterations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS, elapsed, ITERATIONS / elapsed);
}

static void bench_utf8_validate(void)
{
    const char *test_str = "test/topic/with/path";
    size_t str_len = 20;
    volatile bool result = false;

    double start = get_time_sec();

    for (int i = 0; i < ITERATIONS; i++) {
        result = mqtt_utf8_validate(test_str, str_len);
    }

    double elapsed = get_time_sec() - start;
    (void)result;

    printf("UTF-8 validate (20 chars): %d iterations in %.3f sec (%.0f ops/sec)\n",
           ITERATIONS, elapsed, ITERATIONS / elapsed);
}

/*******************************************************************************
 * Main
 ******************************************************************************/

int main(void)
{
    printf("MQTT Codec Benchmark\n");
    printf("====================\n\n");

    printf("Variable-Length Integer Encoding:\n");
    bench_varint_encode_1byte();
    bench_varint_encode_2byte();
    bench_varint_encode_4byte();
    printf("\n");

    printf("Variable-Length Integer Decoding:\n");
    bench_varint_decode_1byte();
    bench_varint_decode_2byte();
    bench_varint_decode_4byte();
    printf("\n");

    printf("Variable-Length Integer Size:\n");
    bench_varint_size();
    printf("\n");

    printf("UTF-8 String Operations:\n");
    bench_utf8_encode();
    bench_utf8_decode();
    bench_utf8_validate();
    printf("\n");

    return 0;
}
