/**
 * @file test_framework.h
 * @brief Minimal unit test framework for MQTT client library
 *
 * A lightweight test framework that provides basic test assertions
 * and test organization without external dependencies.
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Test counters - defined as static in each test file */
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static const char *g_current_test = NULL;

/* Color output for terminals */
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_RESET   "\x1b[0m"

/* Test result macros */
#define TEST_PASS() do { \
    g_tests_passed++; \
} while(0)

#define TEST_FAIL(msg) do { \
    g_tests_failed++; \
    printf(COLOR_RED "    FAIL: %s (line %d): %s" COLOR_RESET "\n", \
           __FILE__, __LINE__, msg); \
} while(0)

/* Assertion macros */
#define ASSERT_TRUE(condition) do { \
    if (!(condition)) { \
        TEST_FAIL("Expected true: " #condition); \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(condition) do { \
    if (condition) { \
        TEST_FAIL("Expected false: " #condition); \
        return; \
    } \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        char _msg[256]; \
        snprintf(_msg, sizeof(_msg), "Expected %lld, got %lld", \
                 (long long)(expected), (long long)(actual)); \
        TEST_FAIL(_msg); \
        return; \
    } \
} while(0)

#define ASSERT_NE(not_expected, actual) do { \
    if ((not_expected) == (actual)) { \
        char _msg[256]; \
        snprintf(_msg, sizeof(_msg), "Expected not %lld", (long long)(not_expected)); \
        TEST_FAIL(_msg); \
        return; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        TEST_FAIL("Expected NULL: " #ptr); \
        return; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        TEST_FAIL("Expected not NULL: " #ptr); \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(expected, actual) do { \
    if (strcmp((expected), (actual)) != 0) { \
        char _msg[256]; \
        snprintf(_msg, sizeof(_msg), "Expected \"%s\", got \"%s\"", \
                 (expected), (actual)); \
        TEST_FAIL(_msg); \
        return; \
    } \
} while(0)

#define ASSERT_MEM_EQ(expected, actual, len) do { \
    if (memcmp((expected), (actual), (len)) != 0) { \
        TEST_FAIL("Memory comparison failed"); \
        return; \
    } \
} while(0)

/* Test definition and running */
#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    g_current_test = #name; \
    g_tests_run++; \
    int _failed_before = g_tests_failed; \
    printf("  Running %s...", #name); \
    fflush(stdout); \
    test_##name(); \
    if (g_tests_failed == _failed_before) { \
        TEST_PASS(); \
        printf(COLOR_GREEN " PASS" COLOR_RESET "\n"); \
    } else { \
        printf("\n"); \
    } \
} while(0)

/* Test suite macros */
#define TEST_SUITE_BEGIN(name) \
    printf(COLOR_YELLOW "\n=== Test Suite: %s ===" COLOR_RESET "\n", name)

#define TEST_SUITE_END() do { \
    printf("\n"); \
    if (g_tests_failed == 0) { \
        printf(COLOR_GREEN "All %d tests passed!" COLOR_RESET "\n", g_tests_run); \
    } else { \
        printf(COLOR_RED "%d of %d tests failed" COLOR_RESET "\n", \
               g_tests_failed, g_tests_run); \
    } \
    return g_tests_failed > 0 ? 1 : 0; \
} while(0)

/* Setup and teardown support */
typedef void (*test_setup_fn)(void);
typedef void (*test_teardown_fn)(void);

static test_setup_fn g_test_setup = NULL;
static test_teardown_fn g_test_teardown = NULL;

#define SET_SETUP(fn) g_test_setup = fn
#define SET_TEARDOWN(fn) g_test_teardown = fn

#define RUN_TEST_WITH_FIXTURE(name) do { \
    g_current_test = #name; \
    g_tests_run++; \
    int _failed_before = g_tests_failed; \
    printf("  Running %s...", #name); \
    fflush(stdout); \
    if (g_test_setup) g_test_setup(); \
    test_##name(); \
    if (g_test_teardown) g_test_teardown(); \
    if (g_tests_failed == _failed_before) { \
        TEST_PASS(); \
        printf(COLOR_GREEN " PASS" COLOR_RESET "\n"); \
    } else { \
        printf("\n"); \
    } \
} while(0)

#endif /* TEST_FRAMEWORK_H */
