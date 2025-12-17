/**
 * @file mqtt_socket_posix.c
 * @brief POSIX Socket Implementation for MQTT Client Library
 *
 * This file provides the POSIX implementation of the socket abstraction layer.
 * It supports IPv4 and IPv6, handles non-blocking connections with timeout,
 * and implements robust error handling with automatic retry for interrupted
 * system calls.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "../mqtt_platform.h"
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <string.h>

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

/**
 * @brief Map system errno to mqtt_error_t
 */
static mqtt_error_t map_errno_to_mqtt_error(int err) {
    switch (err) {
        case 0:
            return MQTT_OK;
        case ENOMEM:
            return MQTT_ERR_NOMEM;
        case EINVAL:
            return MQTT_ERR_INVALID_ARG;
        case ETIMEDOUT:
            return MQTT_ERR_TIMEOUT;
        case ECONNREFUSED:
            return MQTT_ERR_CONN_REFUSED;
        case ECONNRESET:
            return MQTT_ERR_CONN_RESET;
        case ECONNABORTED:
            return MQTT_ERR_CONN_ABORTED;
        case ENETUNREACH:
            return MQTT_ERR_NETWORK_UNREACHABLE;
        case EHOSTUNREACH:
            return MQTT_ERR_HOST_UNREACHABLE;
        case EPIPE:
        case ENOTCONN:
            return MQTT_ERR_CONNECTION_LOST;
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
            return MQTT_ERR_WOULD_BLOCK;
        default:
            return MQTT_ERR_SOCKET;
    }
}

/**
 * @brief Get current socket flags
 */
static int get_socket_flags(mqtt_socket_t sock) {
    return fcntl(sock, F_GETFL, 0);
}

/**
 * @brief Set socket flags
 */
static mqtt_error_t set_socket_flags(mqtt_socket_t sock, int flags) {
    if (fcntl(sock, F_SETFL, flags) == -1) {
        return map_errno_to_mqtt_error(errno);
    }
    return MQTT_OK;
}

/*******************************************************************************
 * Socket Functions
 ******************************************************************************/

mqtt_error_t mqtt_socket_create(mqtt_socket_t *sock) {
    if (sock == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Create TCP socket with IPv4 (can be upgraded to IPv6 during connect) */
    mqtt_socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == MQTT_INVALID_SOCKET) {
        return map_errno_to_mqtt_error(errno);
    }

    /* Enable TCP_NODELAY to disable Nagle's algorithm for lower latency */
    int flag = 1;
    if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == -1) {
        int err = errno;
        close(s);
        return map_errno_to_mqtt_error(err);
    }

    /* Enable SO_KEEPALIVE to detect dead connections */
    flag = 1;
    if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag)) == -1) {
        int err = errno;
        close(s);
        return map_errno_to_mqtt_error(err);
    }

    *sock = s;
    return MQTT_OK;
}

mqtt_error_t mqtt_socket_connect(mqtt_socket_t sock, const char *host,
                                  uint16_t port, uint32_t timeout_ms) {
    if (sock == MQTT_INVALID_SOCKET || host == NULL || port == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Resolve hostname to address */
    struct sockaddr_storage addr;
    memset(&addr, 0, sizeof(addr));

    struct addrinfo hints, *result = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;  /* TCP socket */
    hints.ai_protocol = IPPROTO_TCP;

    /* Convert port to string for getaddrinfo */
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", port);

    int gai_err = getaddrinfo(host, port_str, &hints, &result);
    if (gai_err != 0) {
        return MQTT_ERR_DNS;
    }

    mqtt_error_t ret = MQTT_ERR_CONNECT_FAILED;
    int saved_errno = 0;

    /* Try each address until we successfully connect */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        /* Save original socket flags */
        int orig_flags = get_socket_flags(sock);
        if (orig_flags == -1) {
            saved_errno = errno;
            continue;
        }

        bool was_blocking = !(orig_flags & O_NONBLOCK);

        /* If timeout is specified, switch to non-blocking mode */
        if (timeout_ms > 0 && was_blocking) {
            mqtt_error_t err = set_socket_flags(sock, orig_flags | O_NONBLOCK);
            if (err != MQTT_OK) {
                saved_errno = errno;
                continue;
            }
        }

        /* Attempt connection */
        int connect_result = connect(sock, rp->ai_addr, rp->ai_addrlen);

        if (connect_result == 0) {
            /* Connected immediately */
            ret = MQTT_OK;
        } else if (errno == EINPROGRESS) {
            /* Connection in progress - wait for completion or timeout */
            struct pollfd pfd;
            pfd.fd = sock;
            pfd.events = POLLOUT;
            pfd.revents = 0;

            int poll_result = poll(&pfd, 1, (int)timeout_ms);

            if (poll_result > 0) {
                /* Socket became writable - check for errors */
                int sock_err = 0;
                socklen_t sock_err_len = sizeof(sock_err);

                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &sock_err, &sock_err_len) == 0) {
                    if (sock_err == 0) {
                        ret = MQTT_OK;
                    } else {
                        ret = map_errno_to_mqtt_error(sock_err);
                        saved_errno = sock_err;
                    }
                } else {
                    saved_errno = errno;
                    ret = map_errno_to_mqtt_error(errno);
                }
            } else if (poll_result == 0) {
                /* Timeout */
                ret = MQTT_ERR_TIMEOUT;
            } else {
                /* Poll error (EINTR handled by poll automatically in most cases) */
                saved_errno = errno;
                ret = map_errno_to_mqtt_error(errno);
            }
        } else {
            /* Immediate connection failure */
            saved_errno = errno;
            ret = map_errno_to_mqtt_error(errno);
        }

        /* Restore original blocking mode if we changed it */
        if (timeout_ms > 0 && was_blocking) {
            set_socket_flags(sock, orig_flags);
        }

        /* If connection succeeded, we're done */
        if (ret == MQTT_OK) {
            break;
        }
    }

    freeaddrinfo(result);

    /* If all attempts failed, use the last errno */
    if (ret != MQTT_OK && saved_errno != 0) {
        errno = saved_errno;
    }

    return ret;
}

mqtt_error_t mqtt_socket_close(mqtt_socket_t sock) {
    if (sock == MQTT_INVALID_SOCKET) {
        return MQTT_OK;  /* Already closed */
    }

    /* Shutdown both directions of the connection */
    shutdown(sock, SHUT_RDWR);

    /* Close the socket */
    if (close(sock) == -1) {
        /* Ignore EBADF (bad file descriptor) - socket already closed */
        if (errno != EBADF) {
            return map_errno_to_mqtt_error(errno);
        }
    }

    return MQTT_OK;
}

ssize_t mqtt_socket_send(mqtt_socket_t sock, const void *data, size_t len) {
    if (sock == MQTT_INVALID_SOCKET || data == NULL || len == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    ssize_t sent;

    /* Retry on EINTR (interrupted system call) */
    do {
        sent = send(sock, data, len, MSG_NOSIGNAL);
    } while (sent == -1 && errno == EINTR);

    if (sent == -1) {
        int err = errno;

        /* Map common errors */
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return MQTT_ERR_WOULD_BLOCK;
        } else if (err == EPIPE || err == ECONNRESET || err == ENOTCONN) {
            return MQTT_ERR_CONNECTION_LOST;
        } else if (err == ETIMEDOUT) {
            return MQTT_ERR_TIMEOUT;
        }

        return MQTT_ERR_SEND_FAILED;
    }

    return sent;
}

ssize_t mqtt_socket_recv(mqtt_socket_t sock, void *buf, size_t len) {
    if (sock == MQTT_INVALID_SOCKET || buf == NULL || len == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    ssize_t received;

    /* Retry on EINTR (interrupted system call) */
    do {
        received = recv(sock, buf, len, 0);
    } while (received == -1 && errno == EINTR);

    if (received == -1) {
        int err = errno;

        /* Map common errors */
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return MQTT_ERR_WOULD_BLOCK;
        } else if (err == ECONNRESET || err == ENOTCONN) {
            return MQTT_ERR_CONNECTION_LOST;
        } else if (err == ETIMEDOUT) {
            return MQTT_ERR_TIMEOUT;
        }

        return MQTT_ERR_RECV_FAILED;
    } else if (received == 0) {
        /* Connection closed gracefully by peer */
        return 0;
    }

    return received;
}

mqtt_error_t mqtt_socket_set_blocking(mqtt_socket_t sock, bool blocking) {
    if (sock == MQTT_INVALID_SOCKET) {
        return MQTT_ERR_INVALID_ARG;
    }

    int flags = get_socket_flags(sock);
    if (flags == -1) {
        return map_errno_to_mqtt_error(errno);
    }

    if (blocking) {
        flags &= ~O_NONBLOCK;  /* Clear non-blocking flag */
    } else {
        flags |= O_NONBLOCK;   /* Set non-blocking flag */
    }

    return set_socket_flags(sock, flags);
}

mqtt_error_t mqtt_socket_set_timeout(mqtt_socket_t sock, uint32_t send_ms,
                                      uint32_t recv_ms) {
    if (sock == MQTT_INVALID_SOCKET) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Set send timeout */
    if (send_ms > 0) {
        struct timeval tv;
        tv.tv_sec = send_ms / 1000;
        tv.tv_usec = (send_ms % 1000) * 1000;

        if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1) {
            return map_errno_to_mqtt_error(errno);
        }
    }

    /* Set receive timeout */
    if (recv_ms > 0) {
        struct timeval tv;
        tv.tv_sec = recv_ms / 1000;
        tv.tv_usec = (recv_ms % 1000) * 1000;

        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
            return map_errno_to_mqtt_error(errno);
        }
    }

    return MQTT_OK;
}

int mqtt_socket_get_error(void) {
    return errno;
}

/*******************************************************************************
 * DNS Resolution
 ******************************************************************************/

mqtt_error_t mqtt_resolve_host(const char *host, struct sockaddr_storage *addr) {
    if (host == NULL || addr == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(addr, 0, sizeof(*addr));

    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;  /* TCP socket */
    hints.ai_protocol = IPPROTO_TCP;

    int gai_err = getaddrinfo(host, NULL, &hints, &result);
    if (gai_err != 0) {
        return MQTT_ERR_DNS;
    }

    if (result == NULL) {
        return MQTT_ERR_DNS;
    }

    /* Copy the first result */
    memcpy(addr, result->ai_addr, result->ai_addrlen);

    freeaddrinfo(result);
    return MQTT_OK;
}
