#include "event_bus.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "event_bus";

typedef struct {
    QueueHandle_t queues[EVENT_BUS_MAX_SUBSCRIBERS];
    int count;
} subscriber_list_t;

typedef struct {
    subscriber_list_t subscribers[EVENT_BUS_MAX_EVENT_TYPES];
    SemaphoreHandle_t lock;
    bool initialized;
} event_bus_t;

static event_bus_t bus;

esp_err_t event_bus_init(void)
{
    if (bus.initialized) {
        ESP_LOGW(TAG, "Event bus already initialized");
        return ESP_OK;
    }

    memset(&bus, 0, sizeof(bus));
    bus.lock = xSemaphoreCreateMutex();
    if (bus.lock == NULL) {
        ESP_LOGE(TAG, "Failed to create event bus mutex");
        return ESP_ERR_NO_MEM;
    }

    bus.initialized = true;
    ESP_LOGI(TAG, "Event bus initialized (%d types, %d max subscribers per type)",
             EVENT_BUS_MAX_EVENT_TYPES, EVENT_BUS_MAX_SUBSCRIBERS);
    return ESP_OK;
}

esp_err_t event_bus_subscribe(event_type_t type, QueueHandle_t subscriber_queue)
{
    if (type >= EVT_TYPE_COUNT || subscriber_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!bus.initialized) {
        ESP_LOGE(TAG, "Event bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(bus.lock, portMAX_DELAY);

    subscriber_list_t *list = &bus.subscribers[type];

    // Check for duplicate subscription
    for (int i = 0; i < list->count; i++) {
        if (list->queues[i] == subscriber_queue) {
            xSemaphoreGive(bus.lock);
            ESP_LOGW(TAG, "Queue already subscribed to event type %d", type);
            return ESP_OK;
        }
    }

    if (list->count >= EVENT_BUS_MAX_SUBSCRIBERS) {
        xSemaphoreGive(bus.lock);
        ESP_LOGE(TAG, "Max subscribers reached for event type %d", type);
        return ESP_ERR_NO_MEM;
    }

    list->queues[list->count] = subscriber_queue;
    list->count++;

    xSemaphoreGive(bus.lock);
    ESP_LOGD(TAG, "Subscribed to event type %d (total: %d)", type, list->count);
    return ESP_OK;
}

esp_err_t event_bus_unsubscribe(event_type_t type, QueueHandle_t subscriber_queue)
{
    if (type >= EVT_TYPE_COUNT || subscriber_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!bus.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(bus.lock, portMAX_DELAY);

    subscriber_list_t *list = &bus.subscribers[type];

    for (int i = 0; i < list->count; i++) {
        if (list->queues[i] == subscriber_queue) {
            // Shift remaining entries down
            for (int j = i; j < list->count - 1; j++) {
                list->queues[j] = list->queues[j + 1];
            }
            list->queues[list->count - 1] = NULL;
            list->count--;

            xSemaphoreGive(bus.lock);
            ESP_LOGD(TAG, "Unsubscribed from event type %d (remaining: %d)", type, list->count);
            return ESP_OK;
        }
    }

    xSemaphoreGive(bus.lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t event_bus_publish(const event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!bus.initialized) {
        ESP_LOGE(TAG, "Event bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (event->type >= EVT_TYPE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read subscriber list under lock (fast copy of the pointers)
    subscriber_list_t snapshot;
    xSemaphoreTake(bus.lock, portMAX_DELAY);
    memcpy(&snapshot, &bus.subscribers[event->type], sizeof(subscriber_list_t));
    xSemaphoreGive(bus.lock);

    if (snapshot.count == 0) {
        return ESP_OK; // No subscribers, nothing to do
    }

    int delivered = 0;
    for (int i = 0; i < snapshot.count; i++) {
        if (xQueueSend(snapshot.queues[i], event, 0) == pdTRUE) {
            delivered++;
        } else {
            ESP_LOGW(TAG, "Event type %d dropped for subscriber %d (queue full)", event->type, i);
        }
    }

    ESP_LOGD(TAG, "Published event type %d to %d/%d subscribers", event->type, delivered, snapshot.count);
    return ESP_OK;
}

int event_bus_subscriber_count(event_type_t type)
{
    if (type >= EVT_TYPE_COUNT || !bus.initialized) {
        return 0;
    }

    xSemaphoreTake(bus.lock, portMAX_DELAY);
    int count = bus.subscribers[type].count;
    xSemaphoreGive(bus.lock);
    return count;
}
