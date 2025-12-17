/**
 * @file mqtt_packet.h
 * @brief Internal MQTT Packet Structures and Definitions
 *
 * This header defines the internal packet structures, types, and helper macros
 * used throughout the MQTT client library for packet encoding and decoding.
 *
 * @note This is an internal header - not part of the public API
 */

#ifndef MQTT_PACKET_H
#define MQTT_PACKET_H

#include <stdint.h>
#include <stddef.h>
#include <mqtt/mqtt_config.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Packet Type Enumeration                                                    */
/* ========================================================================== */

/**
 * @brief MQTT packet types (control packet types)
 *
 * These values correspond to the upper 4 bits of the first byte
 * in an MQTT packet's fixed header.
 */
typedef enum mqtt_packet_type {
    MQTT_PACKET_CONNECT     = 1,   /**< Client request to connect to Server */
    MQTT_PACKET_CONNACK     = 2,   /**< Connect acknowledgment */
    MQTT_PACKET_PUBLISH     = 3,   /**< Publish message */
    MQTT_PACKET_PUBACK      = 4,   /**< Publish acknowledgment (QoS 1) */
    MQTT_PACKET_PUBREC      = 5,   /**< Publish received (QoS 2, part 1) */
    MQTT_PACKET_PUBREL      = 6,   /**< Publish release (QoS 2, part 2) */
    MQTT_PACKET_PUBCOMP     = 7,   /**< Publish complete (QoS 2, part 3) */
    MQTT_PACKET_SUBSCRIBE   = 8,   /**< Subscribe request */
    MQTT_PACKET_SUBACK      = 9,   /**< Subscribe acknowledgment */
    MQTT_PACKET_UNSUBSCRIBE = 10,  /**< Unsubscribe request */
    MQTT_PACKET_UNSUBACK    = 11,  /**< Unsubscribe acknowledgment */
    MQTT_PACKET_PINGREQ     = 12,  /**< PING request */
    MQTT_PACKET_PINGRESP    = 13,  /**< PING response */
    MQTT_PACKET_DISCONNECT  = 14,  /**< Disconnect notification */
    MQTT_PACKET_AUTH        = 15   /**< Authentication exchange (MQTT 5.0 only) */
} mqtt_packet_type_t;

/* ========================================================================== */
/* Fixed Header Structure                                                     */
/* ========================================================================== */

/**
 * @brief MQTT fixed header structure
 *
 * The fixed header is present in all MQTT control packets and consists of:
 * - Byte 1: Packet type (bits 7-4) and flags (bits 3-0)
 * - Remaining bytes: Variable-length remaining length field
 */
typedef struct mqtt_fixed_header {
    mqtt_packet_type_t type;    /**< Packet type (from bits 7-4 of first byte) */
    uint8_t flags;              /**< Flags field (bits 3-0): DUP, QoS, RETAIN for PUBLISH */
    uint32_t remaining_len;     /**< Remaining length (variable-length encoded) */
} mqtt_fixed_header_t;

/* ========================================================================== */
/* Fixed Header Byte Manipulation Macros                                     */
/* ========================================================================== */

/**
 * @brief Extract packet type from first byte of fixed header
 * @param byte First byte of MQTT packet
 * @return Packet type (mqtt_packet_type_t)
 */
#define MQTT_PACKET_TYPE(byte)     ((mqtt_packet_type_t)(((byte) >> 4) & 0x0F))

/**
 * @brief Extract flags from first byte of fixed header
 * @param byte First byte of MQTT packet
 * @return Flags value (lower 4 bits)
 */
#define MQTT_PACKET_FLAGS(byte)    ((byte) & 0x0F)

/**
 * @brief Construct first byte of fixed header from type and flags
 * @param type Packet type (mqtt_packet_type_t)
 * @param flags Flags value (4 bits)
 * @return Combined byte value
 */
#define MQTT_MAKE_FIXED_BYTE(type, flags) ((((type) & 0x0F) << 4) | ((flags) & 0x0F))

/* ========================================================================== */
/* PUBLISH Packet Flags Macros                                               */
/* ========================================================================== */

/**
 * @brief Extract DUP flag from PUBLISH flags field
 * @param flags Flags byte (bits 3-0 of first header byte)
 * @return DUP flag value (0 or 1)
 *
 * The DUP flag indicates if this is a re-delivery of a previously
 * attempted QoS > 0 message.
 */
#define MQTT_PUBLISH_DUP(flags)    (((flags) >> 3) & 0x01)

/**
 * @brief Extract QoS level from PUBLISH flags field
 * @param flags Flags byte (bits 3-0 of first header byte)
 * @return QoS level (0, 1, or 2)
 */
#define MQTT_PUBLISH_QOS(flags)    (((flags) >> 1) & 0x03)

/**
 * @brief Extract RETAIN flag from PUBLISH flags field
 * @param flags Flags byte (bits 3-0 of first header byte)
 * @return RETAIN flag value (0 or 1)
 *
 * If set, the broker will store this message for future subscribers.
 */
#define MQTT_PUBLISH_RETAIN(flags) ((flags) & 0x01)

/**
 * @brief Construct PUBLISH flags byte from DUP, QoS, and RETAIN values
 * @param dup DUP flag (0 or 1)
 * @param qos QoS level (0, 1, or 2)
 * @param retain RETAIN flag (0 or 1)
 * @return Combined flags value
 */
#define MQTT_MAKE_PUBLISH_FLAGS(dup, qos, retain) \
    ((((dup) & 1) << 3) | (((qos) & 3) << 1) | ((retain) & 1))

/* ========================================================================== */
/* Reserved Flags for Other Packet Types                                     */
/* ========================================================================== */

/**
 * @brief Reserved flags for PUBREL, SUBSCRIBE, and UNSUBSCRIBE packets
 *
 * These packet types have reserved bits that must be set to specific values
 * as per the MQTT specification.
 */
#define MQTT_FLAGS_PUBREL       0x02  /**< PUBREL must have flags = 0010 */
#define MQTT_FLAGS_SUBSCRIBE    0x02  /**< SUBSCRIBE must have flags = 0010 */
#define MQTT_FLAGS_UNSUBSCRIBE  0x02  /**< UNSUBSCRIBE must have flags = 0010 */

/* ========================================================================== */
/* Maximum Values                                                             */
/* ========================================================================== */

/**
 * @brief Maximum value for variable-length encoded remaining length
 *
 * The MQTT spec allows up to 4 bytes for encoding the remaining length,
 * which gives a maximum value of 268,435,455 (0x0FFFFFFF).
 */
#define MQTT_MAX_REMAINING_LENGTH  268435455u

/**
 * @brief Maximum packet identifier value
 *
 * Packet identifiers are 16-bit values, with 0 reserved.
 * Valid packet IDs are 1-65535.
 */
#define MQTT_MAX_PACKET_ID         65535u

#ifdef __cplusplus
}
#endif

#endif /* MQTT_PACKET_H */
