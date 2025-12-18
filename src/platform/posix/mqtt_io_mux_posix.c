/**
 * @file mqtt_io_mux_posix.c
 * @brief POSIX I/O Multiplexer Implementation
 *
 * Implements the I/O multiplexer abstraction for POSIX systems with support for:
 * - poll() - Portable fallback
 * - epoll - Linux high-performance backend
 * - kqueue - BSD/macOS high-performance backend
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "../mqtt_io_mux.h"
#include "../../memory/mqtt_memory.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>

/* Platform-specific includes */
#ifdef __linux__
    #include <sys/epoll.h>
    #define MQTT_HAS_EPOLL 1
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    #include <sys/types.h>
    #include <sys/event.h>
    #include <sys/time.h>
    #define MQTT_HAS_KQUEUE 1
#endif

/*******************************************************************************
 * Internal Structures
 ******************************************************************************/

/**
 * @brief File descriptor registration entry (for poll backend)
 */
typedef struct mqtt_io_fd_entry {
    mqtt_socket_t fd;          /**< File descriptor */
    uint32_t events;           /**< Registered events */
    void *user_data;           /**< User data */
    bool active;               /**< Entry is in use */
} mqtt_io_fd_entry_t;

/**
 * @brief I/O multiplexer internal structure
 */
struct mqtt_io_mux {
    mqtt_io_mux_type_t type;   /**< Backend type */
    size_t max_events;         /**< Maximum events per wait */

    /* Backend-specific data */
    union {
        /* poll() backend */
        struct {
            struct pollfd *pollfds;     /**< pollfd array */
            mqtt_io_fd_entry_t *entries; /**< FD registry */
            size_t capacity;            /**< Array capacity */
            size_t count;               /**< Number of registered fds */
        } poll;

#ifdef MQTT_HAS_EPOLL
        /* epoll backend */
        struct {
            int epfd;                   /**< epoll file descriptor */
            struct epoll_event *events; /**< Event array */
        } epoll;
#endif

#ifdef MQTT_HAS_KQUEUE
        /* kqueue backend */
        struct {
            int kq;                     /**< kqueue descriptor */
            struct kevent *events;      /**< Event array */
        } kqueue;
#endif
    } backend;
};

/*******************************************************************************
 * Backend Detection
 ******************************************************************************/

mqtt_io_mux_type_t mqtt_io_mux_best_backend(void)
{
#ifdef MQTT_HAS_EPOLL
    return MQTT_IO_MUX_EPOLL;
#elif defined(MQTT_HAS_KQUEUE)
    return MQTT_IO_MUX_KQUEUE;
#else
    return MQTT_IO_MUX_POLL;
#endif
}

const char *mqtt_io_mux_type_name(mqtt_io_mux_type_t type)
{
    switch (type) {
        case MQTT_IO_MUX_AUTO:   return "auto";
        case MQTT_IO_MUX_POLL:   return "poll";
        case MQTT_IO_MUX_EPOLL:  return "epoll";
        case MQTT_IO_MUX_KQUEUE: return "kqueue";
        default:                 return "unknown";
    }
}

/*******************************************************************************
 * poll() Backend Implementation
 ******************************************************************************/

static mqtt_io_mux_t *poll_create(size_t max_events)
{
    mqtt_io_mux_t *mux = mqtt_calloc(1, sizeof(*mux));
    if (!mux) return NULL;

    mux->type = MQTT_IO_MUX_POLL;
    mux->max_events = max_events;

    /* Initial capacity */
    size_t initial_cap = max_events > 16 ? max_events : 16;

    mux->backend.poll.pollfds = mqtt_calloc(initial_cap, sizeof(struct pollfd));
    mux->backend.poll.entries = mqtt_calloc(initial_cap, sizeof(mqtt_io_fd_entry_t));

    if (!mux->backend.poll.pollfds || !mux->backend.poll.entries) {
        mqtt_free(mux->backend.poll.pollfds);
        mqtt_free(mux->backend.poll.entries);
        mqtt_free(mux);
        return NULL;
    }

    mux->backend.poll.capacity = initial_cap;
    mux->backend.poll.count = 0;

    /* Initialize pollfds to invalid */
    for (size_t i = 0; i < initial_cap; i++) {
        mux->backend.poll.pollfds[i].fd = -1;
    }

    return mux;
}

static void poll_destroy(mqtt_io_mux_t *mux)
{
    mqtt_free(mux->backend.poll.pollfds);
    mqtt_free(mux->backend.poll.entries);
}

static int poll_find_slot(mqtt_io_mux_t *mux, mqtt_socket_t fd)
{
    for (size_t i = 0; i < mux->backend.poll.capacity; i++) {
        if (mux->backend.poll.entries[i].active &&
            mux->backend.poll.entries[i].fd == fd) {
            return (int)i;
        }
    }
    return -1;
}

static int poll_find_free_slot(mqtt_io_mux_t *mux)
{
    for (size_t i = 0; i < mux->backend.poll.capacity; i++) {
        if (!mux->backend.poll.entries[i].active) {
            return (int)i;
        }
    }
    return -1;
}

static mqtt_error_t poll_add(mqtt_io_mux_t *mux, mqtt_socket_t fd,
                              uint32_t events, void *user_data)
{
    /* Check if already registered */
    if (poll_find_slot(mux, fd) >= 0) {
        return MQTT_ERR_INVALID_STATE;
    }

    /* Find free slot */
    int slot = poll_find_free_slot(mux);
    if (slot < 0) {
        /* Need to grow arrays */
        size_t new_cap = mux->backend.poll.capacity * 2;
        struct pollfd *new_pollfds = mqtt_realloc(mux->backend.poll.pollfds,
                                                   new_cap * sizeof(struct pollfd));
        mqtt_io_fd_entry_t *new_entries = mqtt_realloc(mux->backend.poll.entries,
                                                        new_cap * sizeof(mqtt_io_fd_entry_t));

        if (!new_pollfds || !new_entries) {
            /* Partial allocation - restore */
            if (new_pollfds && new_pollfds != mux->backend.poll.pollfds) {
                mqtt_free(new_pollfds);
            }
            if (new_entries && new_entries != mux->backend.poll.entries) {
                mqtt_free(new_entries);
            }
            return MQTT_ERR_NOMEM;
        }

        /* Initialize new slots */
        for (size_t i = mux->backend.poll.capacity; i < new_cap; i++) {
            new_pollfds[i].fd = -1;
            new_pollfds[i].events = 0;
            new_pollfds[i].revents = 0;
            memset(&new_entries[i], 0, sizeof(new_entries[i]));
        }

        mux->backend.poll.pollfds = new_pollfds;
        mux->backend.poll.entries = new_entries;
        slot = (int)mux->backend.poll.capacity;
        mux->backend.poll.capacity = new_cap;
    }

    /* Convert events */
    short poll_events = 0;
    if (events & MQTT_IO_READ)  poll_events |= POLLIN;
    if (events & MQTT_IO_WRITE) poll_events |= POLLOUT;

    /* Register */
    mux->backend.poll.pollfds[slot].fd = fd;
    mux->backend.poll.pollfds[slot].events = poll_events;
    mux->backend.poll.pollfds[slot].revents = 0;

    mux->backend.poll.entries[slot].fd = fd;
    mux->backend.poll.entries[slot].events = events;
    mux->backend.poll.entries[slot].user_data = user_data;
    mux->backend.poll.entries[slot].active = true;

    mux->backend.poll.count++;
    return MQTT_OK;
}

static mqtt_error_t poll_modify(mqtt_io_mux_t *mux, mqtt_socket_t fd, uint32_t events)
{
    int slot = poll_find_slot(mux, fd);
    if (slot < 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    short poll_events = 0;
    if (events & MQTT_IO_READ)  poll_events |= POLLIN;
    if (events & MQTT_IO_WRITE) poll_events |= POLLOUT;

    mux->backend.poll.pollfds[slot].events = poll_events;
    mux->backend.poll.entries[slot].events = events;

    return MQTT_OK;
}

static mqtt_error_t poll_remove(mqtt_io_mux_t *mux, mqtt_socket_t fd)
{
    int slot = poll_find_slot(mux, fd);
    if (slot < 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    mux->backend.poll.pollfds[slot].fd = -1;
    mux->backend.poll.pollfds[slot].events = 0;
    mux->backend.poll.entries[slot].active = false;
    mux->backend.poll.count--;

    return MQTT_OK;
}

static int poll_wait(mqtt_io_mux_t *mux, mqtt_io_ready_t *ready,
                      size_t max_ready, int timeout_ms)
{
    /* Count active fds and compact if needed */
    nfds_t nfds = 0;
    for (size_t i = 0; i < mux->backend.poll.capacity; i++) {
        if (mux->backend.poll.pollfds[i].fd >= 0) {
            nfds++;
        }
    }

    if (nfds == 0) {
        /* Nothing to wait on - just sleep if timeout > 0 */
        if (timeout_ms > 0) {
            mqtt_sleep_ms((uint32_t)timeout_ms);
        }
        return 0;
    }

    int ret = poll(mux->backend.poll.pollfds, mux->backend.poll.capacity, timeout_ms);

    if (ret < 0) {
        if (errno == EINTR) {
            return 0;  /* Interrupted - no events */
        }
        return -MQTT_ERR_INTERNAL;
    }

    if (ret == 0) {
        return 0;  /* Timeout */
    }

    /* Collect ready events */
    int count = 0;
    for (size_t i = 0; i < mux->backend.poll.capacity && (size_t)count < max_ready; i++) {
        short revents = mux->backend.poll.pollfds[i].revents;
        if (revents == 0) continue;

        mqtt_io_fd_entry_t *entry = &mux->backend.poll.entries[i];
        if (!entry->active) continue;

        uint32_t events = MQTT_IO_NONE;
        if (revents & POLLIN)  events |= MQTT_IO_READ;
        if (revents & POLLOUT) events |= MQTT_IO_WRITE;
        if (revents & POLLERR) events |= MQTT_IO_ERROR;
        if (revents & POLLHUP) events |= MQTT_IO_HUP;

        ready[count].fd = entry->fd;
        ready[count].events = events;
        ready[count].user_data = entry->user_data;
        count++;
    }

    return count;
}

/*******************************************************************************
 * epoll Backend Implementation (Linux)
 ******************************************************************************/

#ifdef MQTT_HAS_EPOLL

static mqtt_io_mux_t *epoll_create_mux(size_t max_events)
{
    mqtt_io_mux_t *mux = mqtt_calloc(1, sizeof(*mux));
    if (!mux) return NULL;

    mux->type = MQTT_IO_MUX_EPOLL;
    mux->max_events = max_events;

    /* Create epoll instance */
    mux->backend.epoll.epfd = epoll_create1(EPOLL_CLOEXEC);
    if (mux->backend.epoll.epfd < 0) {
        mqtt_free(mux);
        return NULL;
    }

    /* Allocate event array */
    mux->backend.epoll.events = mqtt_calloc(max_events, sizeof(struct epoll_event));
    if (!mux->backend.epoll.events) {
        close(mux->backend.epoll.epfd);
        mqtt_free(mux);
        return NULL;
    }

    return mux;
}

static void epoll_destroy(mqtt_io_mux_t *mux)
{
    close(mux->backend.epoll.epfd);
    mqtt_free(mux->backend.epoll.events);
}

static mqtt_error_t epoll_add(mqtt_io_mux_t *mux, mqtt_socket_t fd,
                               uint32_t events, void *user_data)
{
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));

    ev.data.ptr = user_data;

    if (events & MQTT_IO_READ)  ev.events |= EPOLLIN;
    if (events & MQTT_IO_WRITE) ev.events |= EPOLLOUT;

    if (epoll_ctl(mux->backend.epoll.epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        if (errno == EEXIST) {
            return MQTT_ERR_INVALID_STATE;
        }
        return MQTT_ERR_INTERNAL;
    }

    return MQTT_OK;
}

static mqtt_error_t epoll_modify(mqtt_io_mux_t *mux, mqtt_socket_t fd, uint32_t events)
{
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));

    ev.data.fd = fd;  /* Store fd for retrieval */

    if (events & MQTT_IO_READ)  ev.events |= EPOLLIN;
    if (events & MQTT_IO_WRITE) ev.events |= EPOLLOUT;

    if (epoll_ctl(mux->backend.epoll.epfd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    return MQTT_OK;
}

static mqtt_error_t epoll_remove(mqtt_io_mux_t *mux, mqtt_socket_t fd)
{
    if (epoll_ctl(mux->backend.epoll.epfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        if (errno == ENOENT) {
            return MQTT_OK;  /* Already removed */
        }
        return MQTT_ERR_INVALID_ARG;
    }

    return MQTT_OK;
}

static int epoll_wait_events(mqtt_io_mux_t *mux, mqtt_io_ready_t *ready,
                              size_t max_ready, int timeout_ms)
{
    int nfds = epoll_wait(mux->backend.epoll.epfd,
                          mux->backend.epoll.events,
                          (int)mux->max_events,
                          timeout_ms);

    if (nfds < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -MQTT_ERR_INTERNAL;
    }

    /* Convert events */
    int count = 0;
    for (int i = 0; i < nfds && (size_t)count < max_ready; i++) {
        struct epoll_event *ev = &mux->backend.epoll.events[i];

        uint32_t events = MQTT_IO_NONE;
        if (ev->events & EPOLLIN)  events |= MQTT_IO_READ;
        if (ev->events & EPOLLOUT) events |= MQTT_IO_WRITE;
        if (ev->events & EPOLLERR) events |= MQTT_IO_ERROR;
        if (ev->events & EPOLLHUP) events |= MQTT_IO_HUP;

        ready[count].fd = -1;  /* Not stored in epoll - need fd registry */
        ready[count].events = events;
        ready[count].user_data = ev->data.ptr;
        count++;
    }

    return count;
}

#endif /* MQTT_HAS_EPOLL */

/*******************************************************************************
 * kqueue Backend Implementation (BSD/macOS)
 ******************************************************************************/

#ifdef MQTT_HAS_KQUEUE

static mqtt_io_mux_t *kqueue_create_mux(size_t max_events)
{
    mqtt_io_mux_t *mux = mqtt_calloc(1, sizeof(*mux));
    if (!mux) return NULL;

    mux->type = MQTT_IO_MUX_KQUEUE;
    mux->max_events = max_events;

    /* Create kqueue */
    mux->backend.kqueue.kq = kqueue();
    if (mux->backend.kqueue.kq < 0) {
        mqtt_free(mux);
        return NULL;
    }

    /* Allocate event array */
    mux->backend.kqueue.events = mqtt_calloc(max_events, sizeof(struct kevent));
    if (!mux->backend.kqueue.events) {
        close(mux->backend.kqueue.kq);
        mqtt_free(mux);
        return NULL;
    }

    return mux;
}

static void kqueue_destroy(mqtt_io_mux_t *mux)
{
    close(mux->backend.kqueue.kq);
    mqtt_free(mux->backend.kqueue.events);
}

static mqtt_error_t kqueue_add(mqtt_io_mux_t *mux, mqtt_socket_t fd,
                                uint32_t events, void *user_data)
{
    struct kevent changes[2];
    int nchanges = 0;

    if (events & MQTT_IO_READ) {
        EV_SET(&changes[nchanges], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, user_data);
        nchanges++;
    }

    if (events & MQTT_IO_WRITE) {
        EV_SET(&changes[nchanges], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, user_data);
        nchanges++;
    }

    if (kevent(mux->backend.kqueue.kq, changes, nchanges, NULL, 0, NULL) < 0) {
        return MQTT_ERR_INTERNAL;
    }

    return MQTT_OK;
}

static mqtt_error_t kqueue_modify(mqtt_io_mux_t *mux, mqtt_socket_t fd, uint32_t events)
{
    struct kevent changes[4];
    int nchanges = 0;

    /* Remove both filters first */
    EV_SET(&changes[nchanges], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    nchanges++;
    EV_SET(&changes[nchanges], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    nchanges++;

    /* Apply - ignore errors for delete (filter might not exist) */
    kevent(mux->backend.kqueue.kq, changes, nchanges, NULL, 0, NULL);

    /* Now add desired filters */
    nchanges = 0;
    if (events & MQTT_IO_READ) {
        EV_SET(&changes[nchanges], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
        nchanges++;
    }

    if (events & MQTT_IO_WRITE) {
        EV_SET(&changes[nchanges], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, NULL);
        nchanges++;
    }

    if (nchanges > 0) {
        if (kevent(mux->backend.kqueue.kq, changes, nchanges, NULL, 0, NULL) < 0) {
            return MQTT_ERR_INTERNAL;
        }
    }

    return MQTT_OK;
}

static mqtt_error_t kqueue_remove(mqtt_io_mux_t *mux, mqtt_socket_t fd)
{
    struct kevent changes[2];

    EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);

    /* Ignore errors - filters might not exist */
    kevent(mux->backend.kqueue.kq, changes, 2, NULL, 0, NULL);

    return MQTT_OK;
}

static int kqueue_wait_events(mqtt_io_mux_t *mux, mqtt_io_ready_t *ready,
                               size_t max_ready, int timeout_ms)
{
    struct timespec ts, *pts = NULL;

    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        pts = &ts;
    }

    int nev = kevent(mux->backend.kqueue.kq, NULL, 0,
                     mux->backend.kqueue.events, (int)mux->max_events, pts);

    if (nev < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -MQTT_ERR_INTERNAL;
    }

    /* Convert events - kqueue reports per-filter, merge same fd */
    int count = 0;
    for (int i = 0; i < nev && (size_t)count < max_ready; i++) {
        struct kevent *kev = &mux->backend.kqueue.events[i];

        uint32_t events = MQTT_IO_NONE;
        if (kev->filter == EVFILT_READ)  events |= MQTT_IO_READ;
        if (kev->filter == EVFILT_WRITE) events |= MQTT_IO_WRITE;
        if (kev->flags & EV_ERROR)       events |= MQTT_IO_ERROR;
        if (kev->flags & EV_EOF)         events |= MQTT_IO_HUP;

        ready[count].fd = (mqtt_socket_t)kev->ident;
        ready[count].events = events;
        ready[count].user_data = kev->udata;
        count++;
    }

    return count;
}

#endif /* MQTT_HAS_KQUEUE */

/*******************************************************************************
 * Public API Implementation
 ******************************************************************************/

mqtt_io_mux_t *mqtt_io_mux_create(mqtt_io_mux_type_t type, size_t max_events)
{
    if (max_events == 0) {
        max_events = 64;  /* Default */
    }

    /* Auto-select best backend */
    if (type == MQTT_IO_MUX_AUTO) {
        type = mqtt_io_mux_best_backend();
    }

    switch (type) {
#ifdef MQTT_HAS_EPOLL
        case MQTT_IO_MUX_EPOLL:
            return epoll_create_mux(max_events);
#endif

#ifdef MQTT_HAS_KQUEUE
        case MQTT_IO_MUX_KQUEUE:
            return kqueue_create_mux(max_events);
#endif

        case MQTT_IO_MUX_POLL:
        default:
            return poll_create(max_events);
    }
}

void mqtt_io_mux_destroy(mqtt_io_mux_t *mux)
{
    if (!mux) return;

    switch (mux->type) {
#ifdef MQTT_HAS_EPOLL
        case MQTT_IO_MUX_EPOLL:
            epoll_destroy(mux);
            break;
#endif

#ifdef MQTT_HAS_KQUEUE
        case MQTT_IO_MUX_KQUEUE:
            kqueue_destroy(mux);
            break;
#endif

        case MQTT_IO_MUX_POLL:
        default:
            poll_destroy(mux);
            break;
    }

    mqtt_free(mux);
}

mqtt_error_t mqtt_io_mux_add(mqtt_io_mux_t *mux, mqtt_socket_t fd,
                              uint32_t events, void *user_data)
{
    if (!mux || fd == MQTT_INVALID_SOCKET) {
        return MQTT_ERR_INVALID_ARG;
    }

    switch (mux->type) {
#ifdef MQTT_HAS_EPOLL
        case MQTT_IO_MUX_EPOLL:
            return epoll_add(mux, fd, events, user_data);
#endif

#ifdef MQTT_HAS_KQUEUE
        case MQTT_IO_MUX_KQUEUE:
            return kqueue_add(mux, fd, events, user_data);
#endif

        case MQTT_IO_MUX_POLL:
        default:
            return poll_add(mux, fd, events, user_data);
    }
}

mqtt_error_t mqtt_io_mux_modify(mqtt_io_mux_t *mux, mqtt_socket_t fd, uint32_t events)
{
    if (!mux || fd == MQTT_INVALID_SOCKET) {
        return MQTT_ERR_INVALID_ARG;
    }

    switch (mux->type) {
#ifdef MQTT_HAS_EPOLL
        case MQTT_IO_MUX_EPOLL:
            return epoll_modify(mux, fd, events);
#endif

#ifdef MQTT_HAS_KQUEUE
        case MQTT_IO_MUX_KQUEUE:
            return kqueue_modify(mux, fd, events);
#endif

        case MQTT_IO_MUX_POLL:
        default:
            return poll_modify(mux, fd, events);
    }
}

mqtt_error_t mqtt_io_mux_remove(mqtt_io_mux_t *mux, mqtt_socket_t fd)
{
    if (!mux || fd == MQTT_INVALID_SOCKET) {
        return MQTT_ERR_INVALID_ARG;
    }

    switch (mux->type) {
#ifdef MQTT_HAS_EPOLL
        case MQTT_IO_MUX_EPOLL:
            return epoll_remove(mux, fd);
#endif

#ifdef MQTT_HAS_KQUEUE
        case MQTT_IO_MUX_KQUEUE:
            return kqueue_remove(mux, fd);
#endif

        case MQTT_IO_MUX_POLL:
        default:
            return poll_remove(mux, fd);
    }
}

int mqtt_io_mux_wait(mqtt_io_mux_t *mux, mqtt_io_ready_t *ready,
                      size_t max_ready, int timeout_ms)
{
    if (!mux || !ready || max_ready == 0) {
        return -MQTT_ERR_INVALID_ARG;
    }

    switch (mux->type) {
#ifdef MQTT_HAS_EPOLL
        case MQTT_IO_MUX_EPOLL:
            return epoll_wait_events(mux, ready, max_ready, timeout_ms);
#endif

#ifdef MQTT_HAS_KQUEUE
        case MQTT_IO_MUX_KQUEUE:
            return kqueue_wait_events(mux, ready, max_ready, timeout_ms);
#endif

        case MQTT_IO_MUX_POLL:
        default:
            return poll_wait(mux, ready, max_ready, timeout_ms);
    }
}

mqtt_io_mux_type_t mqtt_io_mux_get_type(mqtt_io_mux_t *mux)
{
    if (!mux) return MQTT_IO_MUX_POLL;
    return mux->type;
}
