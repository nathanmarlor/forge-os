#include "esp_log.h"
#include "bm1370.h"
#include "hashrate_monitor_task.h"
#include "app_context.h"
#include "stats.h"
#include "power_module.h"

#define POLL_RATE 5000

static const char *TAG = "hashrate_monitor";

void hashrate_monitor_task(void *pvParameters)
{
    (void)pvParameters;
    extern app_context_t APP_CONTEXT;
    stats_module_t *stats = &APP_CONTEXT.stats;
    power_module_t *pwr = &APP_CONTEXT.power;

    TickType_t taskWakeTime = xTaskGetTickCount();
    while (1) {
        BM1370_read_registers();

        vTaskDelay(100 / portTICK_PERIOD_MS);

        // Check for frequency changes and reset measurements
        float current_freq = pwr->frequency_value;
        if (current_freq != stats->frequency_value) {
            stats_reset_measurements(stats);
            stats->frequency_value = current_freq;
        }

        stats_compute_hashrate(stats);
        ESP_LOGI(TAG, "Hashrate: %.2f GH/s", stats->hashrate);

        vTaskDelayUntil(&taskWakeTime, POLL_RATE / portTICK_PERIOD_MS);
    }
}
