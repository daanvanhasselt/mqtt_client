/**
 * @file test_io_mux.c
 * @brief Unit tests for I/O Multiplexer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "../../src/platform/mqtt_io_mux.h"

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
 * Test Cases
 ******************************************************************************/

static int test_create_destroy(void)
{
    mqtt_io_mux_t *mux = mqtt_io_mux_create(MQTT_IO_MUX_AUTO, 64);
    ASSERT(mux != NULL);

    mqtt_io_mux_type_t type = mqtt_io_mux_get_type(mux);
    printf("[%s] ", mqtt_io_mux_type_name(type));

    mqtt_io_mux_destroy(mux);
    return 1;
}

static int test_best_backend(void)
{
    mqtt_io_mux_type_t best = mqtt_io_mux_best_backend();

    /* On Linux, should be epoll */
#ifdef __linux__
    ASSERT(best == MQTT_IO_MUX_EPOLL);
#endif

    /* On macOS/BSD, should be kqueue */
#if defined(__APPLE__) || defined(__FreeBSD__)
    ASSERT(best == MQTT_IO_MUX_KQUEUE);
#endif

    return 1;
}

static int test_poll_backend(void)
{
    /* Explicitly create poll backend */
    mqtt_io_mux_t *mux = mqtt_io_mux_create(MQTT_IO_MUX_POLL, 16);
    ASSERT(mux != NULL);
    ASSERT(mqtt_io_mux_get_type(mux) == MQTT_IO_MUX_POLL);

    mqtt_io_mux_destroy(mux);
    return 1;
}

static int test_add_remove(void)
{
    mqtt_io_mux_t *mux = mqtt_io_mux_create(MQTT_IO_MUX_AUTO, 16);
    ASSERT(mux != NULL);

    /* Create a socketpair for testing */
    int fds[2];
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    /* Add first socket */
    mqtt_error_t err = mqtt_io_mux_add(mux, fds[0], MQTT_IO_READ, (void *)0x1234);
    ASSERT(err == MQTT_OK);

    /* Add second socket */
    err = mqtt_io_mux_add(mux, fds[1], MQTT_IO_WRITE, (void *)0x5678);
    ASSERT(err == MQTT_OK);

    /* Remove first socket */
    err = mqtt_io_mux_remove(mux, fds[0]);
    ASSERT(err == MQTT_OK);

    /* Remove second socket */
    err = mqtt_io_mux_remove(mux, fds[1]);
    ASSERT(err == MQTT_OK);

    close(fds[0]);
    close(fds[1]);
    mqtt_io_mux_destroy(mux);
    return 1;
}

static int test_wait_timeout(void)
{
    mqtt_io_mux_t *mux = mqtt_io_mux_create(MQTT_IO_MUX_AUTO, 16);
    ASSERT(mux != NULL);

    mqtt_io_ready_t ready[16];

    /* Wait with no fds registered should timeout immediately */
    int count = mqtt_io_mux_wait(mux, ready, 16, 10);  /* 10ms timeout */
    ASSERT(count == 0);  /* Should timeout with no events */

    mqtt_io_mux_destroy(mux);
    return 1;
}

static int test_read_event(void)
{
    mqtt_io_mux_t *mux = mqtt_io_mux_create(MQTT_IO_MUX_AUTO, 16);
    ASSERT(mux != NULL);

    /* Create socketpair */
    int fds[2];
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    /* Monitor fds[0] for reading */
    mqtt_error_t err = mqtt_io_mux_add(mux, fds[0], MQTT_IO_READ, (void *)(intptr_t)fds[0]);
    ASSERT(err == MQTT_OK);

    /* Write data to fds[1] - this should make fds[0] readable */
    const char *msg = "test";
    ssize_t written = write(fds[1], msg, 4);
    ASSERT(written == 4);

    /* Wait for read event */
    mqtt_io_ready_t ready[16];
    int count = mqtt_io_mux_wait(mux, ready, 16, 100);  /* 100ms timeout */
    ASSERT(count >= 1);
    ASSERT(ready[0].events & MQTT_IO_READ);

    close(fds[0]);
    close(fds[1]);
    mqtt_io_mux_destroy(mux);
    return 1;
}

static int test_write_event(void)
{
    mqtt_io_mux_t *mux = mqtt_io_mux_create(MQTT_IO_MUX_AUTO, 16);
    ASSERT(mux != NULL);

    /* Create socketpair */
    int fds[2];
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    /* Monitor fds[0] for writing */
    mqtt_error_t err = mqtt_io_mux_add(mux, fds[0], MQTT_IO_WRITE, (void *)(intptr_t)fds[0]);
    ASSERT(err == MQTT_OK);

    /* Socket should be immediately writable (buffer is empty) */
    mqtt_io_ready_t ready[16];
    int count = mqtt_io_mux_wait(mux, ready, 16, 100);
    ASSERT(count >= 1);
    ASSERT(ready[0].events & MQTT_IO_WRITE);

    close(fds[0]);
    close(fds[1]);
    mqtt_io_mux_destroy(mux);
    return 1;
}

static int test_modify_events(void)
{
    mqtt_io_mux_t *mux = mqtt_io_mux_create(MQTT_IO_MUX_AUTO, 16);
    ASSERT(mux != NULL);

    /* Create socketpair */
    int fds[2];
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    /* Add monitoring for read only */
    mqtt_error_t err = mqtt_io_mux_add(mux, fds[0], MQTT_IO_READ, NULL);
    ASSERT(err == MQTT_OK);

    /* Modify to monitor write only */
    err = mqtt_io_mux_modify(mux, fds[0], MQTT_IO_WRITE);
    ASSERT(err == MQTT_OK);

    /* Should get write event (socket is writable) */
    mqtt_io_ready_t ready[16];
    int count = mqtt_io_mux_wait(mux, ready, 16, 100);
    ASSERT(count >= 1);
    ASSERT(ready[0].events & MQTT_IO_WRITE);

    close(fds[0]);
    close(fds[1]);
    mqtt_io_mux_destroy(mux);
    return 1;
}

/*******************************************************************************
 * Main
 ******************************************************************************/

int main(void)
{
    printf("I/O Multiplexer Tests\n");
    printf("=====================\n\n");

    printf("Backend detection:\n");
    printf("  Best backend: %s\n\n", mqtt_io_mux_type_name(mqtt_io_mux_best_backend()));

    printf("Running tests:\n");

    TEST(create_destroy);
    TEST(best_backend);
    TEST(poll_backend);
    TEST(add_remove);
    TEST(wait_timeout);
    TEST(read_event);
    TEST(write_event);
    TEST(modify_events);

    printf("\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
