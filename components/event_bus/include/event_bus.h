#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#define EVENT_BUS_MAX_EVENT_TYPES  16
#define EVENT_BUS_MAX_SUBSCRIBERS   8

// Event types
typedef enum {
    EVT_MINING_NOTIFY = 0,    // New work from pool
    EVT_JOB_READY,            // Job built, ready for ASIC
    EVT_NONCE_FOUND,          // ASIC found a nonce
    EVT_SHARE_SUBMIT,         // Share to send to pool
    EVT_SHARE_RESULT,         // Pool accepted/rejected
    EVT_REGISTER_READ,        // ASIC register data
    EVT_HASHRATE_UPDATE,      // New hashrate calculation
    EVT_TEMP_UPDATE,          // Temperature reading
    EVT_POWER_UPDATE,         // Power/voltage reading
    EVT_CONFIG_CHANGED,       // NVS config was modified
    EVT_POOL_STATE,           // Connection state change
    EVT_ABANDON_WORK,         // Clear queues, new block
    EVT_CLOCK_SYNC,           // NTP time from pool
    EVT_TYPE_COUNT            // Must be last
} event_type_t;

_Static_assert(EVT_TYPE_COUNT <= EVENT_BUS_MAX_EVENT_TYPES,
               "Too many event types for EVENT_BUS_MAX_EVENT_TYPES");

// Event payload types

typedef struct {
    float hashrate;           // GH/s
    float error_percentage;
} hashrate_update_event_t;

typedef struct {
    float chip_temp[6];
    float chip_temp_avg;
    float vr_temp;
} temp_update_event_t;

typedef struct {
    float voltage;
    float power;
    float current;
    uint16_t fan_perc;
    uint16_t fan_rpm[2];
} power_update_event_t;

typedef struct {
    char key[32];             // NVS key that changed
    uint32_t new_value;       // New value (for numeric keys)
} config_changed_event_t;

typedef struct {
    bool accepted;
    char error_str[64];
} share_result_event_t;

typedef struct {
    double nonce_diff;
    uint8_t job_id;
} nonce_found_event_t;

typedef struct {
    uint8_t register_type;
    uint8_t asic_nr;
    uint32_t value;
} register_read_event_t;

typedef struct {
    uint8_t state;            // stratum_state_t value
} pool_state_event_t;

typedef struct {
    uint32_t ntime;
} clock_sync_event_t;

// Event envelope
typedef struct {
    event_type_t type;
    uint32_t timestamp_ms;
    union {
        hashrate_update_event_t hashrate;
        temp_update_event_t     temperature;
        power_update_event_t    power;
        config_changed_event_t  config;
        share_result_event_t    share_result;
        nonce_found_event_t     nonce_found;
        register_read_event_t   register_read;
        pool_state_event_t      pool_state;
        clock_sync_event_t      clock_sync;
    } data;
} event_t;

/**
 * Initialize the event bus. Must be called before any subscribe/publish.
 */
esp_err_t event_bus_init(void);

/**
 * Subscribe a FreeRTOS queue to receive events of a given type.
 * The subscriber_queue must be sized to hold event_t items.
 *
 * @param type           Event type to subscribe to
 * @param subscriber_queue  Queue handle to receive events
 * @return ESP_OK on success, ESP_ERR_NO_MEM if max subscribers reached,
 *         ESP_ERR_INVALID_ARG if type is out of range or queue is NULL
 */
esp_err_t event_bus_subscribe(event_type_t type, QueueHandle_t subscriber_queue);

/**
 * Unsubscribe a queue from a given event type.
 *
 * @param type           Event type to unsubscribe from
 * @param subscriber_queue  Queue handle to remove
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not subscribed
 */
esp_err_t event_bus_unsubscribe(event_type_t type, QueueHandle_t subscriber_queue);

/**
 * Publish an event to all subscribers of that event type.
 * Non-blocking: if a subscriber's queue is full, the event is dropped
 * for that subscriber (with a warning log).
 *
 * @param event  Event to publish (copied into subscriber queues)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if event is NULL
 */
esp_err_t event_bus_publish(const event_t *event);

/**
 * Get the number of subscribers for a given event type.
 * Useful for testing and diagnostics.
 */
int event_bus_subscriber_count(event_type_t type);

/**
 * Helper: create a timestamped event with the given type.
 */
static inline event_t event_create(event_type_t type) {
    event_t evt = {0};
    evt.type = type;
    evt.timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    return evt;
}

#endif // EVENT_BUS_H
