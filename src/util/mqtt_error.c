/**
 * @file mqtt_error.c
 * @brief MQTT error code handling implementation
 */

#include "mqtt/mqtt_error.h"

const char *mqtt_error_str(mqtt_error_t err)
{
    switch (err) {
        /* Success */
        case MQTT_OK:
            return "Success";

        /* General errors */
        case MQTT_ERR_NOMEM:
            return "Out of memory";
        case MQTT_ERR_INVALID_ARG:
            return "Invalid argument";
        case MQTT_ERR_NOT_IMPLEMENTED:
            return "Not implemented";
        case MQTT_ERR_INTERNAL:
            return "Internal error";

        /* Connection errors */
        case MQTT_ERR_SOCKET:
            return "Socket error";
        case MQTT_ERR_DNS:
            return "DNS resolution failed";
        case MQTT_ERR_CONNECT_FAILED:
            return "Connection failed";
        case MQTT_ERR_TLS_HANDSHAKE:
            return "TLS handshake failed";
        case MQTT_ERR_TIMEOUT:
            return "Operation timed out";
        case MQTT_ERR_CONN_REFUSED:
            return "Connection refused";
        case MQTT_ERR_CONN_RESET:
            return "Connection reset by peer";
        case MQTT_ERR_CONN_ABORTED:
            return "Connection aborted";
        case MQTT_ERR_NETWORK_UNREACHABLE:
            return "Network unreachable";
        case MQTT_ERR_HOST_UNREACHABLE:
            return "Host unreachable";

        /* Protocol errors */
        case MQTT_ERR_PROTOCOL:
            return "Protocol error";
        case MQTT_ERR_MALFORMED_PACKET:
            return "Malformed packet";
        case MQTT_ERR_PACKET_TOO_LARGE:
            return "Packet too large";
        case MQTT_ERR_UNSUPPORTED_VERSION:
            return "Unsupported protocol version";
        case MQTT_ERR_INVALID_PACKET_TYPE:
            return "Invalid packet type";
        case MQTT_ERR_INVALID_QOS:
            return "Invalid QoS level";
        case MQTT_ERR_INVALID_UTF8:
            return "Invalid UTF-8 encoding";
        case MQTT_ERR_INVALID_TOPIC:
            return "Invalid topic";
        case MQTT_ERR_INVALID_PACKET_ID:
            return "Invalid packet ID";

        /* State errors */
        case MQTT_ERR_NOT_CONNECTED:
            return "Not connected";
        case MQTT_ERR_ALREADY_CONNECTED:
            return "Already connected";
        case MQTT_ERR_INFLIGHT_FULL:
            return "In-flight message queue full";
        case MQTT_ERR_PACKET_ID_EXHAUSTED:
            return "No packet IDs available";
        case MQTT_ERR_INVALID_STATE:
            return "Invalid state for operation";
        case MQTT_ERR_SUBSCRIPTION_NOT_FOUND:
            return "Subscription not found";
        case MQTT_ERR_DISCONNECTING:
            return "Client is disconnecting";
        case MQTT_ERR_MAX_RETRIES:
            return "Maximum retransmission attempts exceeded";

        /* Async errors */
        case MQTT_ERR_WOULD_BLOCK:
            return "Operation would block";
        case MQTT_ERR_PENDING:
            return "Operation pending";
        case MQTT_ERR_CANCELLED:
            return "Operation cancelled";
        case MQTT_ERR_IN_PROGRESS:
            return "Operation in progress";

        /* I/O errors */
        case MQTT_ERR_SEND_FAILED:
            return "Send failed";
        case MQTT_ERR_RECV_FAILED:
            return "Receive failed";
        case MQTT_ERR_CONNECTION_LOST:
            return "Connection lost";
        case MQTT_ERR_EOF:
            return "End of stream";
        case MQTT_ERR_BUFFER_OVERFLOW:
            return "Buffer overflow";
        case MQTT_ERR_BUFFER_UNDERFLOW:
            return "Buffer underflow";

        /* MQTT v3.1.1 CONNACK errors */
        case MQTT_ERR_V311_UNACCEPTABLE_PROTOCOL:
            return "Unacceptable protocol version";
        case MQTT_ERR_V311_IDENTIFIER_REJECTED:
            return "Client identifier rejected";
        case MQTT_ERR_V311_SERVER_UNAVAILABLE:
            return "Server unavailable";
        case MQTT_ERR_V311_BAD_CREDENTIALS:
            return "Bad username or password";
        case MQTT_ERR_V311_NOT_AUTHORIZED:
            return "Not authorized";

        /* MQTT v5 reason codes */
        case MQTT_ERR_V5_UNSPECIFIED:
            return "Unspecified error (v5)";
        case MQTT_ERR_V5_MALFORMED_PACKET:
            return "Malformed packet (v5)";
        case MQTT_ERR_V5_PROTOCOL_ERROR:
            return "Protocol error (v5)";
        case MQTT_ERR_V5_IMPL_SPECIFIC:
            return "Implementation specific error (v5)";
        case MQTT_ERR_V5_UNSUPPORTED_VERSION:
            return "Unsupported protocol version (v5)";
        case MQTT_ERR_V5_CLIENT_ID_INVALID:
            return "Client ID invalid (v5)";
        case MQTT_ERR_V5_BAD_CREDENTIALS:
            return "Bad username or password (v5)";
        case MQTT_ERR_V5_NOT_AUTHORIZED:
            return "Not authorized (v5)";
        case MQTT_ERR_V5_SERVER_UNAVAILABLE:
            return "Server unavailable (v5)";
        case MQTT_ERR_V5_SERVER_BUSY:
            return "Server busy (v5)";
        case MQTT_ERR_V5_BANNED:
            return "Banned (v5)";
        case MQTT_ERR_V5_BAD_AUTH_METHOD:
            return "Bad authentication method (v5)";
        case MQTT_ERR_V5_TOPIC_INVALID:
            return "Topic name invalid (v5)";
        case MQTT_ERR_V5_PACKET_TOO_LARGE:
            return "Packet too large (v5)";
        case MQTT_ERR_V5_QUOTA_EXCEEDED:
            return "Quota exceeded (v5)";
        case MQTT_ERR_V5_PAYLOAD_FORMAT_INVALID:
            return "Payload format invalid (v5)";
        case MQTT_ERR_V5_RETAIN_NOT_SUPPORTED:
            return "Retain not supported (v5)";
        case MQTT_ERR_V5_QOS_NOT_SUPPORTED:
            return "QoS not supported (v5)";
        case MQTT_ERR_V5_USE_ANOTHER_SERVER:
            return "Use another server (v5)";
        case MQTT_ERR_V5_SERVER_MOVED:
            return "Server moved (v5)";
        case MQTT_ERR_V5_RATE_EXCEEDED:
            return "Connection rate exceeded (v5)";

        default:
            return "Unknown error";
    }
}

int mqtt_error_is_connection(mqtt_error_t err)
{
    return (err <= -100 && err >= -199);
}

int mqtt_error_is_protocol(mqtt_error_t err)
{
    return (err <= -200 && err >= -299);
}

int mqtt_error_is_temporary(mqtt_error_t err)
{
    return (err == MQTT_ERR_TIMEOUT ||
            err == MQTT_ERR_WOULD_BLOCK ||
            err == MQTT_ERR_PENDING ||
            err == MQTT_ERR_IN_PROGRESS ||
            err == MQTT_ERR_V5_SERVER_BUSY);
}

int mqtt_error_is_fatal(mqtt_error_t err)
{
    return (err == MQTT_ERR_PROTOCOL ||
            err == MQTT_ERR_MALFORMED_PACKET ||
            err == MQTT_ERR_UNSUPPORTED_VERSION ||
            err == MQTT_ERR_CONNECTION_LOST ||
            err == MQTT_ERR_CONN_RESET ||
            (err <= -600 && err >= -799));  /* CONNACK/v5 rejections */
}
