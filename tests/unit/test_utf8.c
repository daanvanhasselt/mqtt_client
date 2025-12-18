/**
 * @file test_utf8.c
 * @brief Unit tests for MQTT UTF-8 encoding/decoding/validation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/core/mqtt_utf8.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  Testing %s... ", #name); \
    if (test_##name()) { \
        printf("PASSED\n"); \
        tests_passed++; \
    } else { \
        printf("FAILED\n"); \
    } \
    tests_run++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\n    ASSERT failed: %s (line %d)\n", #cond, __LINE__); \
        return 0; \
    } \
} while(0)

/*******************************************************************************
 * Encode Tests
 ******************************************************************************/

static int test_encode_simple(void)
{
    const char *str = "test";
    uint8_t buf[10];

    int len = mqtt_utf8_encode(str, 4, buf, sizeof(buf));
    ASSERT(len == 6);  /* 2 bytes length + 4 bytes data */

    /* Check length prefix (big-endian) */
    ASSERT(buf[0] == 0x00);
    ASSERT(buf[1] == 0x04);

    /* Check string data */
    ASSERT(memcmp(&buf[2], "test", 4) == 0);

    return 1;
}

static int test_encode_empty(void)
{
    uint8_t buf[10];

    int len = mqtt_utf8_encode("", 0, buf, sizeof(buf));
    ASSERT(len == 2);  /* Just length prefix */

    ASSERT(buf[0] == 0x00);
    ASSERT(buf[1] == 0x00);

    return 1;
}

static int test_encode_max_length(void)
{
    /* Test encoding a string at max length boundary */
    char *str = malloc(256);
    ASSERT(str != NULL);
    memset(str, 'x', 256);

    uint8_t *buf = malloc(260);
    ASSERT(buf != NULL);

    int len = mqtt_utf8_encode(str, 256, buf, 260);
    ASSERT(len == 258);  /* 2 + 256 */

    /* Check length prefix: 0x0100 = 256 */
    ASSERT(buf[0] == 0x01);
    ASSERT(buf[1] == 0x00);

    free(str);
    free(buf);
    return 1;
}

static int test_encode_buffer_too_small(void)
{
    const char *str = "test";
    uint8_t buf[5];  /* Need 6 bytes */

    int len = mqtt_utf8_encode(str, 4, buf, sizeof(buf));
    ASSERT(len == -1);  /* Should fail */

    return 1;
}

static int test_encode_null_args(void)
{
    uint8_t buf[10];
    const char *str = "test";

    /* NULL buffer */
    ASSERT(mqtt_utf8_encode(str, 4, NULL, 10) == -1);

    /* NULL string with non-zero length */
    ASSERT(mqtt_utf8_encode(NULL, 4, buf, sizeof(buf)) == -1);

    /* NULL string with zero length should work */
    ASSERT(mqtt_utf8_encode(NULL, 0, buf, sizeof(buf)) == 2);

    return 1;
}

/*******************************************************************************
 * Decode Tests
 ******************************************************************************/

static int test_decode_simple(void)
{
    uint8_t data[] = {0x00, 0x04, 't', 'e', 's', 't'};
    const char *str;
    uint16_t len;

    int consumed = mqtt_utf8_decode(data, sizeof(data), &str, &len);
    ASSERT(consumed == 6);
    ASSERT(len == 4);
    ASSERT(memcmp(str, "test", 4) == 0);

    return 1;
}

static int test_decode_empty(void)
{
    uint8_t data[] = {0x00, 0x00};
    const char *str;
    uint16_t len;

    int consumed = mqtt_utf8_decode(data, sizeof(data), &str, &len);
    ASSERT(consumed == 2);
    ASSERT(len == 0);

    return 1;
}

static int test_decode_incomplete_header(void)
{
    uint8_t data[] = {0x00};  /* Only 1 byte */
    const char *str;
    uint16_t len;

    int consumed = mqtt_utf8_decode(data, sizeof(data), &str, &len);
    ASSERT(consumed == -1);  /* Should fail */

    return 1;
}

static int test_decode_incomplete_data(void)
{
    uint8_t data[] = {0x00, 0x04, 't', 'e'};  /* Says 4 bytes, only 2 */
    const char *str;
    uint16_t len;

    int consumed = mqtt_utf8_decode(data, sizeof(data), &str, &len);
    ASSERT(consumed == -1);  /* Should fail */

    return 1;
}

static int test_decode_null_args(void)
{
    uint8_t data[] = {0x00, 0x04, 't', 'e', 's', 't'};
    const char *str;
    uint16_t len;

    ASSERT(mqtt_utf8_decode(NULL, 6, &str, &len) == -1);
    ASSERT(mqtt_utf8_decode(data, 6, NULL, &len) == -1);
    ASSERT(mqtt_utf8_decode(data, 6, &str, NULL) == -1);

    return 1;
}

/*******************************************************************************
 * Validation Tests
 ******************************************************************************/

static int test_validate_ascii(void)
{
    /* Simple ASCII is valid */
    ASSERT(mqtt_utf8_validate("hello", 5) == true);
    ASSERT(mqtt_utf8_validate("test/topic", 10) == true);
    ASSERT(mqtt_utf8_validate("123", 3) == true);

    return 1;
}

static int test_validate_empty(void)
{
    /* Empty string is valid */
    ASSERT(mqtt_utf8_validate("", 0) == true);

    return 1;
}

static int test_validate_multibyte(void)
{
    /* UTF-8 multibyte characters */
    const char *utf8_2byte = "\xC2\xA9";  /* Copyright symbol */
    ASSERT(mqtt_utf8_validate(utf8_2byte, 2) == true);

    const char *utf8_3byte = "\xE2\x82\xAC";  /* Euro sign */
    ASSERT(mqtt_utf8_validate(utf8_3byte, 3) == true);

    const char *utf8_4byte = "\xF0\x9F\x98\x80";  /* Emoji */
    ASSERT(mqtt_utf8_validate(utf8_4byte, 4) == true);

    return 1;
}

static int test_validate_null_char(void)
{
    /* NULL character (U+0000) is not allowed in MQTT */
    const char with_null[] = {'t', 'e', '\0', 's', 't'};
    ASSERT(mqtt_utf8_validate(with_null, 5) == false);

    return 1;
}

static int test_validate_invalid_utf8(void)
{
    /* Invalid UTF-8 sequences */

    /* Invalid continuation byte */
    const char invalid1[] = {'\x80'};
    ASSERT(mqtt_utf8_validate(invalid1, 1) == false);

    /* Incomplete 2-byte sequence */
    const char invalid2[] = {'\xC2'};
    ASSERT(mqtt_utf8_validate(invalid2, 1) == false);

    /* Incomplete 3-byte sequence */
    const char invalid3[] = {'\xE2', '\x82'};
    ASSERT(mqtt_utf8_validate(invalid3, 2) == false);

    /* Overlong encoding (2-byte for ASCII) */
    const char overlong[] = {'\xC0', '\x80'};  /* Should be just '\0' */
    ASSERT(mqtt_utf8_validate(overlong, 2) == false);

    return 1;
}

static int test_validate_surrogate(void)
{
    /* UTF-16 surrogates (U+D800-U+DFFF) are not valid UTF-8 */
    const char surrogate[] = {'\xED', '\xA0', '\x80'};  /* U+D800 */
    ASSERT(mqtt_utf8_validate(surrogate, 3) == false);

    return 1;
}

/*******************************************************************************
 * Roundtrip Tests
 ******************************************************************************/

static int test_roundtrip(void)
{
    const char *original = "hello/world";
    uint8_t buf[20];

    /* Encode */
    int encoded_len = mqtt_utf8_encode(original, strlen(original), buf, sizeof(buf));
    ASSERT(encoded_len == 13);  /* 2 + 11 */

    /* Decode */
    const char *decoded;
    uint16_t decoded_len;
    int consumed = mqtt_utf8_decode(buf, (size_t)encoded_len, &decoded, &decoded_len);
    ASSERT(consumed == 13);
    ASSERT(decoded_len == 11);
    ASSERT(memcmp(decoded, original, decoded_len) == 0);

    return 1;
}

/*******************************************************************************
 * Main
 ******************************************************************************/

int main(void)
{
    printf("UTF-8 Tests\n");
    printf("===========\n\n");

    printf("Encode tests:\n");
    TEST(encode_simple);
    TEST(encode_empty);
    TEST(encode_max_length);
    TEST(encode_buffer_too_small);
    TEST(encode_null_args);

    printf("\nDecode tests:\n");
    TEST(decode_simple);
    TEST(decode_empty);
    TEST(decode_incomplete_header);
    TEST(decode_incomplete_data);
    TEST(decode_null_args);

    printf("\nValidation tests:\n");
    TEST(validate_ascii);
    TEST(validate_empty);
    TEST(validate_multibyte);
    TEST(validate_null_char);
    TEST(validate_invalid_utf8);
    TEST(validate_surrogate);

    printf("\nRoundtrip tests:\n");
    TEST(roundtrip);

    printf("\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
