/**
 * @file mqtt_socket_windows.c
 * @brief Windows Socket Implementation (Stub)
 *
 * This file provides stub implementations for Windows socket operations.
 * TODO: Implement full Winsock2 support.
 */

#if defined(_WIN32) || defined(_WIN64)

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include "../mqtt_platform.h"
#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

static mqtt_error_t map_wsa_error(int err) {
    switch (err) {
        case 0:
            return MQTT_OK;
        case WSAEWOULDBLOCK:
            return MQTT_ERR_WOULD_BLOCK;
        case WSAETIMEDOUT:
            return MQTT_ERR_TIMEOUT;
        case WSAECONNREFUSED:
            return MQTT_ERR_CONN_REFUSED;
        case WSAECONNRESET:
            return MQTT_ERR_CONN_RESET;
        case WSAECONNABORTED:
            return MQTT_ERR_CONN_ABORTED;
        case WSAENETUNREACH:
            return MQTT_ERR_NETWORK_UNREACHABLE;
        case WSAEHOSTUNREACH:
            return MQTT_ERR_HOST_UNREACHABLE;
        case WSAENOTCONN:
            return MQTT_ERR_CONNECTION_LOST;
        default:
            return MQTT_ERR_SOCKET;
    }
}

/*******************************************************************************
 * Socket Functions
 ******************************************************************************/

mqtt_error_t mqtt_socket_create(mqtt_socket_t *sock) {
    if (sock == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Initialize Winsock */
    static int wsa_initialized = 0;
    if (!wsa_initialized) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            return MQTT_ERR_SOCKET;
        }
        wsa_initialized = 1;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return map_wsa_error(WSAGetLastError());
    }

    /* Enable TCP_NODELAY */
    int flag = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));

    /* Enable SO_KEEPALIVE */
    flag = 1;
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char *)&flag, sizeof(flag));

    *sock = s;
    return MQTT_OK;
}

mqtt_error_t mqtt_socket_connect(mqtt_socket_t sock, const char *host,
                                  uint16_t port, uint32_t timeout_ms) {
    if (sock == MQTT_INVALID_SOCKET || host == NULL || port == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Resolve hostname */
    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", port);

    if (getaddrinfo(host, port_str, &hints, &result) != 0) {
        return MQTT_ERR_DNS;
    }

    mqtt_error_t ret = MQTT_ERR_CONNECT_FAILED;

    /* Set non-blocking for timeout */
    if (timeout_ms > 0) {
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
    }

    /* Try each address */
    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        int result_code = connect(sock, rp->ai_addr, (int)rp->ai_addrlen);

        if (result_code == 0) {
            ret = MQTT_OK;
            break;
        }

        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            /* Wait for connection with select */
            fd_set writefds;
            FD_ZERO(&writefds);
            FD_SET(sock, &writefds);

            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

            int select_result = select(0, NULL, &writefds, NULL, &tv);

            if (select_result > 0) {
                int sock_err = 0;
                int sock_err_len = sizeof(sock_err);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&sock_err, &sock_err_len);

                if (sock_err == 0) {
                    ret = MQTT_OK;
                    break;
                } else {
                    ret = map_wsa_error(sock_err);
                }
            } else if (select_result == 0) {
                ret = MQTT_ERR_TIMEOUT;
            } else {
                ret = map_wsa_error(WSAGetLastError());
            }
        } else {
            ret = map_wsa_error(err);
        }
    }

    freeaddrinfo(result);

    /* Restore blocking mode */
    if (timeout_ms > 0) {
        u_long mode = 0;
        ioctlsocket(sock, FIONBIO, &mode);
    }

    return ret;
}

mqtt_error_t mqtt_socket_close(mqtt_socket_t sock) {
    if (sock == MQTT_INVALID_SOCKET) {
        return MQTT_OK;
    }

    shutdown(sock, SD_BOTH);
    closesocket(sock);
    return MQTT_OK;
}

ssize_t mqtt_socket_send(mqtt_socket_t sock, const void *data, size_t len) {
    if (sock == MQTT_INVALID_SOCKET || data == NULL || len == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    int sent = send(sock, (const char *)data, (int)len, 0);
    if (sent == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return MQTT_ERR_WOULD_BLOCK;
        } else if (err == WSAECONNRESET || err == WSAENOTCONN) {
            return MQTT_ERR_CONNECTION_LOST;
        }
        return MQTT_ERR_SEND_FAILED;
    }

    return sent;
}

ssize_t mqtt_socket_recv(mqtt_socket_t sock, void *buf, size_t len) {
    if (sock == MQTT_INVALID_SOCKET || buf == NULL || len == 0) {
        return MQTT_ERR_INVALID_ARG;
    }

    int received = recv(sock, (char *)buf, (int)len, 0);
    if (received == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return MQTT_ERR_WOULD_BLOCK;
        } else if (err == WSAECONNRESET || err == WSAENOTCONN) {
            return MQTT_ERR_CONNECTION_LOST;
        }
        return MQTT_ERR_RECV_FAILED;
    } else if (received == 0) {
        return 0;  /* Connection closed */
    }

    return received;
}

mqtt_error_t mqtt_socket_set_blocking(mqtt_socket_t sock, bool blocking) {
    if (sock == MQTT_INVALID_SOCKET) {
        return MQTT_ERR_INVALID_ARG;
    }

    u_long mode = blocking ? 0 : 1;
    if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
        return map_wsa_error(WSAGetLastError());
    }

    return MQTT_OK;
}

mqtt_error_t mqtt_socket_set_timeout(mqtt_socket_t sock, uint32_t send_ms,
                                      uint32_t recv_ms) {
    if (sock == MQTT_INVALID_SOCKET) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (send_ms > 0) {
        DWORD timeout = send_ms;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
    }

    if (recv_ms > 0) {
        DWORD timeout = recv_ms;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    }

    return MQTT_OK;
}

int mqtt_socket_get_error(void) {
    return WSAGetLastError();
}

mqtt_error_t mqtt_resolve_host(const char *host, struct sockaddr_storage *addr) {
    if (host == NULL || addr == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(addr, 0, sizeof(*addr));

    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, NULL, &hints, &result) != 0) {
        return MQTT_ERR_DNS;
    }

    if (result == NULL) {
        return MQTT_ERR_DNS;
    }

    memcpy(addr, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    return MQTT_OK;
}

#endif /* _WIN32 || _WIN64 */
