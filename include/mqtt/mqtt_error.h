/**
 * @file mqtt_error.h
 * @brief MQTT Error Codes and Error Handling
 *
 * This file defines all error codes used throughout the MQTT client library
 * and provides functions for converting error codes to human-readable strings.
 *
 * Error codes are organized into logical categories:
 * - General errors (memory, arguments)
 * - Connection errors (socket, DNS, TLS)
 * - Protocol errors (malformed packets, version mismatches)
 * - State errors (connection state, resource exhaustion)
 * - Async errors (non-blocking operations)
 * - I/O errors (send/receive failures)
 *
 * @note All functions return MQTT_OK (0) on success. Any non-zero value
 *       indicates an error condition.
 */

#ifndef MQTT_ERROR_H
#define MQTT_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <mqtt/mqtt_config.h>

/*******************************************************************************
 * Error Code Enumeration
 ******************************************************************************/

/**
 * @enum mqtt_error_t
 * @brief MQTT error codes
 *
 * All error codes are negative except MQTT_OK which is 0.
 * This allows for easy error checking: if (result != MQTT_OK) { ... }
 */
typedef enum mqtt_error {
    /***************************************************************************
     * Success Code
     **************************************************************************/

    /** Operation completed successfully */
    MQTT_OK = 0,

    /***************************************************************************
     * General Errors (-1 to -99)
     **************************************************************************/

    /** Out of memory */
    MQTT_ERR_NOMEM = -1,

    /** Invalid argument provided */
    MQTT_ERR_INVALID_ARG = -2,

    /** Feature not implemented */
    MQTT_ERR_NOT_IMPLEMENTED = -3,

    /** Internal error (should not occur) */
    MQTT_ERR_INTERNAL = -4,

    /***************************************************************************
     * Connection Errors (-100 to -199)
     **************************************************************************/

    /** Socket creation or operation failed */
    MQTT_ERR_SOCKET = -100,

    /** DNS resolution failed */
    MQTT_ERR_DNS = -101,

    /** Connection to broker failed */
    MQTT_ERR_CONNECT_FAILED = -102,

    /** TLS/SSL handshake failed */
    MQTT_ERR_TLS_HANDSHAKE = -103,

    /** Operation timed out */
    MQTT_ERR_TIMEOUT = -104,

    /** Connection refused by broker */
    MQTT_ERR_CONN_REFUSED = -105,

    /** Connection reset by peer */
    MQTT_ERR_CONN_RESET = -106,

    /** Connection aborted */
    MQTT_ERR_CONN_ABORTED = -107,

    /** Network unreachable */
    MQTT_ERR_NETWORK_UNREACHABLE = -108,

    /** Host unreachable */
    MQTT_ERR_HOST_UNREACHABLE = -109,

    /***************************************************************************
     * Protocol Errors (-200 to -299)
     **************************************************************************/

    /** Protocol violation or unexpected response */
    MQTT_ERR_PROTOCOL = -200,

    /** Malformed packet received */
    MQTT_ERR_MALFORMED_PACKET = -201,

    /** Packet size exceeds maximum allowed */
    MQTT_ERR_PACKET_TOO_LARGE = -202,

    /** Unsupported MQTT protocol version */
    MQTT_ERR_UNSUPPORTED_VERSION = -203,

    /** Invalid packet type for current state */
    MQTT_ERR_INVALID_PACKET_TYPE = -204,

    /** Invalid QoS level */
    MQTT_ERR_INVALID_QOS = -205,

    /** Invalid UTF-8 string encoding */
    MQTT_ERR_INVALID_UTF8 = -206,

    /** Topic name/filter is invalid */
    MQTT_ERR_INVALID_TOPIC = -207,

    /** Packet identifier is invalid or duplicate */
    MQTT_ERR_INVALID_PACKET_ID = -208,

    /***************************************************************************
     * State Errors (-300 to -399)
     **************************************************************************/

    /** Client is not connected to broker */
    MQTT_ERR_NOT_CONNECTED = -300,

    /** Client is already connected */
    MQTT_ERR_ALREADY_CONNECTED = -301,

    /** In-flight message queue is full */
    MQTT_ERR_INFLIGHT_FULL = -302,

    /** No more packet IDs available */
    MQTT_ERR_PACKET_ID_EXHAUSTED = -303,

    /** Operation is not allowed in current state */
    MQTT_ERR_INVALID_STATE = -304,

    /** Subscription not found */
    MQTT_ERR_SUBSCRIPTION_NOT_FOUND = -305,

    /** Client is disconnecting */
    MQTT_ERR_DISCONNECTING = -306,

    /** Maximum retransmission attempts exceeded for QoS 1/2 message */
    MQTT_ERR_MAX_RETRIES = -307,

    /***************************************************************************
     * Async Operation Errors (-400 to -499)
     **************************************************************************/

    /** Operation would block (non-blocking mode) */
    MQTT_ERR_WOULD_BLOCK = -400,

    /** Asynchronous operation is pending */
    MQTT_ERR_PENDING = -401,

    /** Operation was cancelled */
    MQTT_ERR_CANCELLED = -402,

    /** Operation already in progress */
    MQTT_ERR_IN_PROGRESS = -403,

    /***************************************************************************
     * I/O Errors (-500 to -599)
     **************************************************************************/

    /** Failed to send data */
    MQTT_ERR_SEND_FAILED = -500,

    /** Failed to receive data */
    MQTT_ERR_RECV_FAILED = -501,

    /** Connection lost unexpectedly */
    MQTT_ERR_CONNECTION_LOST = -502,

    /** End of stream reached */
    MQTT_ERR_EOF = -503,

    /** Buffer overflow */
    MQTT_ERR_BUFFER_OVERFLOW = -504,

    /** Buffer underflow */
    MQTT_ERR_BUFFER_UNDERFLOW = -505,

    /***************************************************************************
     * MQTT v3.1.1 CONNACK Return Codes (-600 to -699)
     **************************************************************************/

    /** Connection refused: unacceptable protocol version */
    MQTT_ERR_V311_UNACCEPTABLE_PROTOCOL = -600,

    /** Connection refused: identifier rejected */
    MQTT_ERR_V311_IDENTIFIER_REJECTED = -601,

    /** Connection refused: server unavailable */
    MQTT_ERR_V311_SERVER_UNAVAILABLE = -602,

    /** Connection refused: bad username or password */
    MQTT_ERR_V311_BAD_CREDENTIALS = -603,

    /** Connection refused: not authorized */
    MQTT_ERR_V311_NOT_AUTHORIZED = -604,

    /***************************************************************************
     * MQTT v5 Reason Codes (subset) (-700 to -799)
     **************************************************************************/

    /** MQTT v5: Unspecified error */
    MQTT_ERR_V5_UNSPECIFIED = -700,

    /** MQTT v5: Malformed packet */
    MQTT_ERR_V5_MALFORMED_PACKET = -701,

    /** MQTT v5: Protocol error */
    MQTT_ERR_V5_PROTOCOL_ERROR = -702,

    /** MQTT v5: Implementation specific error */
    MQTT_ERR_V5_IMPL_SPECIFIC = -703,

    /** MQTT v5: Unsupported protocol version */
    MQTT_ERR_V5_UNSUPPORTED_VERSION = -704,

    /** MQTT v5: Client identifier not valid */
    MQTT_ERR_V5_CLIENT_ID_INVALID = -705,

    /** MQTT v5: Bad username or password */
    MQTT_ERR_V5_BAD_CREDENTIALS = -706,

    /** MQTT v5: Not authorized */
    MQTT_ERR_V5_NOT_AUTHORIZED = -707,

    /** MQTT v5: Server unavailable */
    MQTT_ERR_V5_SERVER_UNAVAILABLE = -708,

    /** MQTT v5: Server busy */
    MQTT_ERR_V5_SERVER_BUSY = -709,

    /** MQTT v5: Banned */
    MQTT_ERR_V5_BANNED = -710,

    /** MQTT v5: Bad authentication method */
    MQTT_ERR_V5_BAD_AUTH_METHOD = -711,

    /** MQTT v5: Topic name invalid */
    MQTT_ERR_V5_TOPIC_INVALID = -712,

    /** MQTT v5: Packet too large */
    MQTT_ERR_V5_PACKET_TOO_LARGE = -713,

    /** MQTT v5: Quota exceeded */
    MQTT_ERR_V5_QUOTA_EXCEEDED = -714,

    /** MQTT v5: Payload format invalid */
    MQTT_ERR_V5_PAYLOAD_FORMAT_INVALID = -715,

    /** MQTT v5: Retain not supported */
    MQTT_ERR_V5_RETAIN_NOT_SUPPORTED = -716,

    /** MQTT v5: QoS not supported */
    MQTT_ERR_V5_QOS_NOT_SUPPORTED = -717,

    /** MQTT v5: Use another server */
    MQTT_ERR_V5_USE_ANOTHER_SERVER = -718,

    /** MQTT v5: Server moved */
    MQTT_ERR_V5_SERVER_MOVED = -719,

    /** MQTT v5: Connection rate exceeded */
    MQTT_ERR_V5_RATE_EXCEEDED = -720

} mqtt_error_t;

/*******************************************************************************
 * Error Handling Functions
 ******************************************************************************/

/**
 * @brief Convert error code to human-readable string
 *
 * Returns a static string describing the given error code. The returned
 * string should not be modified or freed by the caller.
 *
 * @param err Error code to convert
 * @return Pointer to static string describing the error
 *
 * @note The returned string is always valid and never NULL.
 *       Unknown error codes return "Unknown error".
 *
 * @example
 * @code
 * mqtt_error_t err = mqtt_connect(client, &opts);
 * if (err != MQTT_OK) {
 *     fprintf(stderr, "Connection failed: %s\n", mqtt_error_str(err));
 * }
 * @endcode
 */
MQTT_API const char *mqtt_error_str(mqtt_error_t err);

/**
 * @brief Check if error code indicates a connection error
 *
 * @param err Error code to check
 * @return 1 if error is connection-related, 0 otherwise
 */
MQTT_API int mqtt_error_is_connection(mqtt_error_t err);

/**
 * @brief Check if error code indicates a protocol error
 *
 * @param err Error code to check
 * @return 1 if error is protocol-related, 0 otherwise
 */
MQTT_API int mqtt_error_is_protocol(mqtt_error_t err);

/**
 * @brief Check if error code indicates a temporary/recoverable error
 *
 * Temporary errors include timeouts, would-block conditions, and
 * resource exhaustion that may be resolved by retrying.
 *
 * @param err Error code to check
 * @return 1 if error is temporary, 0 otherwise
 */
MQTT_API int mqtt_error_is_temporary(mqtt_error_t err);

/**
 * @brief Check if error code indicates a fatal error
 *
 * Fatal errors require the connection to be closed and re-established.
 *
 * @param err Error code to check
 * @return 1 if error is fatal, 0 otherwise
 */
MQTT_API int mqtt_error_is_fatal(mqtt_error_t err);

/*******************************************************************************
 * Success Check Macros
 ******************************************************************************/

/**
 * @brief Check if operation succeeded
 * @param err Error code
 * @return 1 if successful, 0 otherwise
 */
#define MQTT_SUCCEEDED(err) ((err) == MQTT_OK)

/**
 * @brief Check if operation failed
 * @param err Error code
 * @return 1 if failed, 0 otherwise
 */
#define MQTT_FAILED(err) ((err) != MQTT_OK)

#ifdef __cplusplus
}
#endif

#endif /* MQTT_ERROR_H */
