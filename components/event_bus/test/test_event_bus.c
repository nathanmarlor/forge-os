#include "unity.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

// ---------- Init tests ----------

TEST_CASE("event_bus_init succeeds", "[event_bus]")
{
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
    // Double init should also succeed
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_init());
}

// ---------- Subscribe tests ----------

TEST_CASE("subscribe with NULL queue returns INVALID_ARG", "[event_bus]")
{
    event_bus_init();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_bus_subscribe(EVT_MINING_NOTIFY, NULL));
}

TEST_CASE("subscribe with invalid event type returns INVALID_ARG", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t q = xQueueCreate(4, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_bus_subscribe(EVT_TYPE_COUNT, q));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_bus_subscribe(99, q));
    vQueueDelete(q);
}

TEST_CASE("subscribe increments subscriber count", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t q1 = xQueueCreate(4, sizeof(event_t));
    QueueHandle_t q2 = xQueueCreate(4, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q1);
    TEST_ASSERT_NOT_NULL(q2);

    TEST_ASSERT_EQUAL(0, event_bus_subscriber_count(EVT_HASHRATE_UPDATE));

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_HASHRATE_UPDATE, q1));
    TEST_ASSERT_EQUAL(1, event_bus_subscriber_count(EVT_HASHRATE_UPDATE));

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_HASHRATE_UPDATE, q2));
    TEST_ASSERT_EQUAL(2, event_bus_subscriber_count(EVT_HASHRATE_UPDATE));

    // Different event type should be unaffected
    TEST_ASSERT_EQUAL(0, event_bus_subscriber_count(EVT_TEMP_UPDATE));

    // Unsubscribe before deleting to avoid dangling pointers in the bus
    event_bus_unsubscribe(EVT_HASHRATE_UPDATE, q1);
    event_bus_unsubscribe(EVT_HASHRATE_UPDATE, q2);
    vQueueDelete(q1);
    vQueueDelete(q2);
}

TEST_CASE("duplicate subscribe is idempotent", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t q = xQueueCreate(4, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q);

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_POWER_UPDATE, q));
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_POWER_UPDATE, q));
    TEST_ASSERT_EQUAL(1, event_bus_subscriber_count(EVT_POWER_UPDATE));

    event_bus_unsubscribe(EVT_POWER_UPDATE, q);
    vQueueDelete(q);
}

TEST_CASE("subscribe up to max subscribers", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t queues[EVENT_BUS_MAX_SUBSCRIBERS + 1];

    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        queues[i] = xQueueCreate(2, sizeof(event_t));
        TEST_ASSERT_NOT_NULL(queues[i]);
        TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_CONFIG_CHANGED, queues[i]));
    }
    TEST_ASSERT_EQUAL(EVENT_BUS_MAX_SUBSCRIBERS, event_bus_subscriber_count(EVT_CONFIG_CHANGED));

    // One more should fail
    queues[EVENT_BUS_MAX_SUBSCRIBERS] = xQueueCreate(2, sizeof(event_t));
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, event_bus_subscribe(EVT_CONFIG_CHANGED, queues[EVENT_BUS_MAX_SUBSCRIBERS]));

    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        event_bus_unsubscribe(EVT_CONFIG_CHANGED, queues[i]);
    }
    for (int i = 0; i <= EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        vQueueDelete(queues[i]);
    }
}

// ---------- Unsubscribe tests ----------

TEST_CASE("unsubscribe removes subscriber", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t q = xQueueCreate(4, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q);

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_POOL_STATE, q));
    TEST_ASSERT_EQUAL(1, event_bus_subscriber_count(EVT_POOL_STATE));

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_unsubscribe(EVT_POOL_STATE, q));
    TEST_ASSERT_EQUAL(0, event_bus_subscriber_count(EVT_POOL_STATE));

    vQueueDelete(q);
}

TEST_CASE("unsubscribe non-existent returns NOT_FOUND", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t q = xQueueCreate(4, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, event_bus_unsubscribe(EVT_POOL_STATE, q));

    vQueueDelete(q);
}

TEST_CASE("unsubscribe with invalid args returns INVALID_ARG", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t q = xQueueCreate(4, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_bus_unsubscribe(EVT_TYPE_COUNT, q));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_bus_unsubscribe(EVT_POOL_STATE, NULL));

    vQueueDelete(q);
}

// ---------- Publish tests ----------

TEST_CASE("publish NULL event returns INVALID_ARG", "[event_bus]")
{
    event_bus_init();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_bus_publish(NULL));
}

TEST_CASE("publish with no subscribers succeeds silently", "[event_bus]")
{
    event_bus_init();
    event_t evt = event_create(EVT_ABANDON_WORK);
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_publish(&evt));
}

TEST_CASE("publish delivers event to single subscriber", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t q = xQueueCreate(4, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q);

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_HASHRATE_UPDATE, q));

    event_t evt = event_create(EVT_HASHRATE_UPDATE);
    evt.data.hashrate.hashrate = 42.5f;
    evt.data.hashrate.error_percentage = 1.2f;

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_publish(&evt));

    event_t received;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(q, &received, 0));
    TEST_ASSERT_EQUAL(EVT_HASHRATE_UPDATE, received.type);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.5f, received.data.hashrate.hashrate);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.2f, received.data.hashrate.error_percentage);

    event_bus_unsubscribe(EVT_HASHRATE_UPDATE, q);
    vQueueDelete(q);
}

TEST_CASE("publish delivers event to multiple subscribers", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t q1 = xQueueCreate(4, sizeof(event_t));
    QueueHandle_t q2 = xQueueCreate(4, sizeof(event_t));
    QueueHandle_t q3 = xQueueCreate(4, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q1);
    TEST_ASSERT_NOT_NULL(q2);
    TEST_ASSERT_NOT_NULL(q3);

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_TEMP_UPDATE, q1));
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_TEMP_UPDATE, q2));
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_TEMP_UPDATE, q3));

    event_t evt = event_create(EVT_TEMP_UPDATE);
    evt.data.temperature.chip_temp_avg = 65.0f;
    evt.data.temperature.vr_temp = 80.0f;

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_publish(&evt));

    event_t received;
    // All three should receive the event
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(q1, &received, 0));
    TEST_ASSERT_EQUAL(EVT_TEMP_UPDATE, received.type);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 65.0f, received.data.temperature.chip_temp_avg);

    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(q2, &received, 0));
    TEST_ASSERT_EQUAL(EVT_TEMP_UPDATE, received.type);

    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(q3, &received, 0));
    TEST_ASSERT_EQUAL(EVT_TEMP_UPDATE, received.type);

    event_bus_unsubscribe(EVT_TEMP_UPDATE, q1);
    event_bus_unsubscribe(EVT_TEMP_UPDATE, q2);
    event_bus_unsubscribe(EVT_TEMP_UPDATE, q3);
    vQueueDelete(q1);
    vQueueDelete(q2);
    vQueueDelete(q3);
}

TEST_CASE("publish drops event when subscriber queue is full", "[event_bus]")
{
    event_bus_init();
    // Queue with capacity 1
    QueueHandle_t q = xQueueCreate(1, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q);

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_POWER_UPDATE, q));

    event_t evt1 = event_create(EVT_POWER_UPDATE);
    evt1.data.power.voltage = 5.0f;
    event_t evt2 = event_create(EVT_POWER_UPDATE);
    evt2.data.power.voltage = 12.0f;

    // First publish fills the queue
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_publish(&evt1));
    // Second publish should still return OK but event is dropped
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_publish(&evt2));

    // Only the first event should be in the queue
    event_t received;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(q, &received, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, received.data.power.voltage);
    // Queue should now be empty
    TEST_ASSERT_EQUAL(pdFALSE, xQueueReceive(q, &received, 0));

    event_bus_unsubscribe(EVT_POWER_UPDATE, q);
    vQueueDelete(q);
}

TEST_CASE("publish only delivers to subscribers of that event type", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t q_temp = xQueueCreate(4, sizeof(event_t));
    QueueHandle_t q_power = xQueueCreate(4, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q_temp);
    TEST_ASSERT_NOT_NULL(q_power);

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_TEMP_UPDATE, q_temp));
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_POWER_UPDATE, q_power));

    event_t evt = event_create(EVT_TEMP_UPDATE);
    evt.data.temperature.chip_temp_avg = 55.0f;
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_publish(&evt));

    // Temp subscriber should receive it
    event_t received;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(q_temp, &received, 0));
    TEST_ASSERT_EQUAL(EVT_TEMP_UPDATE, received.type);

    // Power subscriber should NOT receive it
    TEST_ASSERT_EQUAL(pdFALSE, xQueueReceive(q_power, &received, 0));

    event_bus_unsubscribe(EVT_TEMP_UPDATE, q_temp);
    event_bus_unsubscribe(EVT_POWER_UPDATE, q_power);
    vQueueDelete(q_temp);
    vQueueDelete(q_power);
}

// ---------- event_create helper test ----------

TEST_CASE("event_create sets type and timestamp", "[event_bus]")
{
    event_t evt = event_create(EVT_SHARE_RESULT);
    TEST_ASSERT_EQUAL(EVT_SHARE_RESULT, evt.type);
    // Timestamp should be non-negative (tick-based)
    TEST_ASSERT_GREATER_OR_EQUAL(0, evt.timestamp_ms);
}

// ---------- Config changed event payload ----------

TEST_CASE("config changed event preserves payload", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t q = xQueueCreate(4, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q);

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_CONFIG_CHANGED, q));

    event_t evt = event_create(EVT_CONFIG_CHANGED);
    strncpy(evt.data.config.key, "asic_freq", sizeof(evt.data.config.key));
    evt.data.config.new_value = 550;

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_publish(&evt));

    event_t received;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(q, &received, 0));
    TEST_ASSERT_EQUAL(EVT_CONFIG_CHANGED, received.type);
    TEST_ASSERT_EQUAL_STRING("asic_freq", received.data.config.key);
    TEST_ASSERT_EQUAL(550, received.data.config.new_value);

    event_bus_unsubscribe(EVT_CONFIG_CHANGED, q);
    vQueueDelete(q);
}

// ---------- Unsubscribe then publish ----------

TEST_CASE("unsubscribe prevents future event delivery", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t q = xQueueCreate(4, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q);

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_NONCE_FOUND, q));
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_unsubscribe(EVT_NONCE_FOUND, q));

    event_t evt = event_create(EVT_NONCE_FOUND);
    evt.data.nonce_found.nonce_diff = 1024.0;
    TEST_ASSERT_EQUAL(ESP_OK, event_bus_publish(&evt));

    event_t received;
    TEST_ASSERT_EQUAL(pdFALSE, xQueueReceive(q, &received, 0));

    vQueueDelete(q);
}

// ---------- Register read event payload ----------

TEST_CASE("register read event preserves all fields", "[event_bus]")
{
    event_bus_init();
    QueueHandle_t q = xQueueCreate(4, sizeof(event_t));
    TEST_ASSERT_NOT_NULL(q);

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_subscribe(EVT_REGISTER_READ, q));

    event_t evt = event_create(EVT_REGISTER_READ);
    evt.data.register_read.register_type = 3;
    evt.data.register_read.asic_nr = 1;
    evt.data.register_read.value = 0xDEADBEEF;

    TEST_ASSERT_EQUAL(ESP_OK, event_bus_publish(&evt));

    event_t received;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(q, &received, 0));
    TEST_ASSERT_EQUAL(3, received.data.register_read.register_type);
    TEST_ASSERT_EQUAL(1, received.data.register_read.asic_nr);
    TEST_ASSERT_EQUAL(0xDEADBEEF, received.data.register_read.value);

    event_bus_unsubscribe(EVT_REGISTER_READ, q);
    vQueueDelete(q);
}
