/**
 * @file mqtt_subscription_store.h
 * @brief Subscription tracking for session restoration
 *
 * Tracks active subscriptions so they can be restored after reconnection
 * when clean_session=false and session_present=false (server lost session).
 */

#ifndef MQTT_SUBSCRIPTION_STORE_H
#define MQTT_SUBSCRIPTION_STORE_H

#include <mqtt/mqtt_types.h>
#include <mqtt/mqtt_error.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Single subscription entry
 */
typedef struct mqtt_subscription_entry {
    char *topic_filter;              /**< Topic filter string (owned) */
    mqtt_qos_t granted_qos;          /**< QoS granted by broker */

#ifdef MQTT_ENABLE_V5
    uint32_t subscription_id;        /**< MQTT 5.0 subscription identifier */
    bool no_local;                   /**< MQTT 5.0: don't receive own messages */
    bool retain_as_published;        /**< MQTT 5.0: retain flag preservation */
    uint8_t retain_handling;         /**< MQTT 5.0: retain handling option */
#endif
} mqtt_subscription_entry_t;

/**
 * @brief Subscription store for tracking active subscriptions
 */
typedef struct mqtt_subscription_store {
    mqtt_subscription_entry_t *entries;  /**< Array of subscriptions */
    size_t count;                        /**< Number of active subscriptions */
    size_t capacity;                     /**< Allocated capacity */
} mqtt_subscription_store_t;

/**
 * @brief Initialize subscription store
 * @param store Store to initialize
 * @return MQTT_OK on success
 */
mqtt_error_t mqtt_subscription_store_init(mqtt_subscription_store_t *store);

/**
 * @brief Cleanup subscription store and free all entries
 * @param store Store to cleanup
 */
void mqtt_subscription_store_cleanup(mqtt_subscription_store_t *store);

/**
 * @brief Add or update a subscription
 * @param store Subscription store
 * @param topic_filter Topic filter to add
 * @param granted_qos QoS granted by broker
 * @return MQTT_OK on success
 */
mqtt_error_t mqtt_subscription_store_add(mqtt_subscription_store_t *store,
                                          const char *topic_filter,
                                          mqtt_qos_t granted_qos);

#ifdef MQTT_ENABLE_V5
/**
 * @brief Add or update a subscription with MQTT 5.0 options
 * @param store Subscription store
 * @param topic_filter Topic filter to add
 * @param granted_qos QoS granted by broker
 * @param subscription_id MQTT 5.0 subscription identifier
 * @param no_local MQTT 5.0 no local option
 * @param retain_as_published MQTT 5.0 retain as published option
 * @param retain_handling MQTT 5.0 retain handling option
 * @return MQTT_OK on success
 */
mqtt_error_t mqtt_subscription_store_add_v5(mqtt_subscription_store_t *store,
                                             const char *topic_filter,
                                             mqtt_qos_t granted_qos,
                                             uint32_t subscription_id,
                                             bool no_local,
                                             bool retain_as_published,
                                             uint8_t retain_handling);
#endif

/**
 * @brief Remove a subscription by topic filter
 * @param store Subscription store
 * @param topic_filter Topic filter to remove
 * @return MQTT_OK if found and removed, MQTT_ERR_NOT_FOUND otherwise
 */
mqtt_error_t mqtt_subscription_store_remove(mqtt_subscription_store_t *store,
                                             const char *topic_filter);

/**
 * @brief Clear all subscriptions
 * @param store Subscription store
 */
void mqtt_subscription_store_clear(mqtt_subscription_store_t *store);

/**
 * @brief Get subscription count
 * @param store Subscription store
 * @return Number of subscriptions
 */
size_t mqtt_subscription_store_count(const mqtt_subscription_store_t *store);

/**
 * @brief Get subscription entry by index (for iteration)
 * @param store Subscription store
 * @param index Entry index
 * @return Pointer to entry or NULL if index out of bounds
 */
const mqtt_subscription_entry_t *mqtt_subscription_store_get(
    const mqtt_subscription_store_t *store, size_t index);

/**
 * @brief Find subscription by topic filter
 * @param store Subscription store
 * @param topic_filter Topic filter to find
 * @return Pointer to entry or NULL if not found
 */
const mqtt_subscription_entry_t *mqtt_subscription_store_find(
    const mqtt_subscription_store_t *store, const char *topic_filter);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_SUBSCRIPTION_STORE_H */
