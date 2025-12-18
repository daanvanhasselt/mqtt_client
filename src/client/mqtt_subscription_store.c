/**
 * @file mqtt_subscription_store.c
 * @brief Subscription tracking implementation
 */

#include "mqtt_subscription_store.h"
#include "../memory/mqtt_memory.h"
#include <string.h>

#define INITIAL_CAPACITY 8

mqtt_error_t mqtt_subscription_store_init(mqtt_subscription_store_t *store)
{
    if (!store) {
        return MQTT_ERR_INVALID_ARG;
    }

    memset(store, 0, sizeof(*store));
    return MQTT_OK;
}

void mqtt_subscription_store_cleanup(mqtt_subscription_store_t *store)
{
    if (!store) return;

    mqtt_subscription_store_clear(store);

    if (store->entries) {
        mqtt_free(store->entries);
        store->entries = NULL;
    }
    store->capacity = 0;
}

static mqtt_error_t ensure_capacity(mqtt_subscription_store_t *store)
{
    if (store->count < store->capacity) {
        return MQTT_OK;
    }

    size_t new_capacity = store->capacity == 0 ? INITIAL_CAPACITY : store->capacity * 2;
    mqtt_subscription_entry_t *new_entries = mqtt_realloc(
        store->entries, new_capacity * sizeof(mqtt_subscription_entry_t));

    if (!new_entries) {
        return MQTT_ERR_NOMEM;
    }

    store->entries = new_entries;
    store->capacity = new_capacity;
    return MQTT_OK;
}

static size_t find_index(const mqtt_subscription_store_t *store, const char *topic_filter)
{
    for (size_t i = 0; i < store->count; i++) {
        if (store->entries[i].topic_filter &&
            strcmp(store->entries[i].topic_filter, topic_filter) == 0) {
            return i;
        }
    }
    return (size_t)-1;  /* Not found */
}

mqtt_error_t mqtt_subscription_store_add(mqtt_subscription_store_t *store,
                                          const char *topic_filter,
                                          mqtt_qos_t granted_qos)
{
    if (!store || !topic_filter) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Check if already exists */
    size_t idx = find_index(store, topic_filter);
    if (idx != (size_t)-1) {
        /* Update existing */
        store->entries[idx].granted_qos = granted_qos;
        return MQTT_OK;
    }

    /* Add new */
    mqtt_error_t err = ensure_capacity(store);
    if (err != MQTT_OK) {
        return err;
    }

    char *topic_copy = mqtt_strdup(topic_filter);
    if (!topic_copy) {
        return MQTT_ERR_NOMEM;
    }

    mqtt_subscription_entry_t *entry = &store->entries[store->count];
    memset(entry, 0, sizeof(*entry));
    entry->topic_filter = topic_copy;
    entry->granted_qos = granted_qos;

    store->count++;
    return MQTT_OK;
}

#ifdef MQTT_ENABLE_V5
mqtt_error_t mqtt_subscription_store_add_v5(mqtt_subscription_store_t *store,
                                             const char *topic_filter,
                                             mqtt_qos_t granted_qos,
                                             uint32_t subscription_id,
                                             bool no_local,
                                             bool retain_as_published,
                                             uint8_t retain_handling)
{
    if (!store || !topic_filter) {
        return MQTT_ERR_INVALID_ARG;
    }

    /* Check if already exists */
    size_t idx = find_index(store, topic_filter);
    if (idx != (size_t)-1) {
        /* Update existing */
        mqtt_subscription_entry_t *entry = &store->entries[idx];
        entry->granted_qos = granted_qos;
        entry->subscription_id = subscription_id;
        entry->no_local = no_local;
        entry->retain_as_published = retain_as_published;
        entry->retain_handling = retain_handling;
        return MQTT_OK;
    }

    /* Add new */
    mqtt_error_t err = ensure_capacity(store);
    if (err != MQTT_OK) {
        return err;
    }

    char *topic_copy = mqtt_strdup(topic_filter);
    if (!topic_copy) {
        return MQTT_ERR_NOMEM;
    }

    mqtt_subscription_entry_t *entry = &store->entries[store->count];
    memset(entry, 0, sizeof(*entry));
    entry->topic_filter = topic_copy;
    entry->granted_qos = granted_qos;
    entry->subscription_id = subscription_id;
    entry->no_local = no_local;
    entry->retain_as_published = retain_as_published;
    entry->retain_handling = retain_handling;

    store->count++;
    return MQTT_OK;
}
#endif

mqtt_error_t mqtt_subscription_store_remove(mqtt_subscription_store_t *store,
                                             const char *topic_filter)
{
    if (!store || !topic_filter) {
        return MQTT_ERR_INVALID_ARG;
    }

    size_t idx = find_index(store, topic_filter);
    if (idx == (size_t)-1) {
        return MQTT_ERR_SUBSCRIPTION_NOT_FOUND;
    }

    /* Free the topic filter string */
    mqtt_free(store->entries[idx].topic_filter);

    /* Move last entry to fill the gap (swap-remove) */
    if (idx < store->count - 1) {
        store->entries[idx] = store->entries[store->count - 1];
    }

    store->count--;
    return MQTT_OK;
}

void mqtt_subscription_store_clear(mqtt_subscription_store_t *store)
{
    if (!store) return;

    for (size_t i = 0; i < store->count; i++) {
        if (store->entries[i].topic_filter) {
            mqtt_free(store->entries[i].topic_filter);
        }
    }
    store->count = 0;
}

size_t mqtt_subscription_store_count(const mqtt_subscription_store_t *store)
{
    return store ? store->count : 0;
}

const mqtt_subscription_entry_t *mqtt_subscription_store_get(
    const mqtt_subscription_store_t *store, size_t index)
{
    if (!store || index >= store->count) {
        return NULL;
    }
    return &store->entries[index];
}

const mqtt_subscription_entry_t *mqtt_subscription_store_find(
    const mqtt_subscription_store_t *store, const char *topic_filter)
{
    if (!store || !topic_filter) {
        return NULL;
    }

    size_t idx = find_index(store, topic_filter);
    if (idx == (size_t)-1) {
        return NULL;
    }
    return &store->entries[idx];
}
