#include "system.h"
#include "work_queue.h"
#include "serial.h"
#include <string.h>
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "asic.h"
#include "app_context.h"
#include "asic_module.h"

static const char *TAG = "ASIC_task";

void ASIC_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    extern app_context_t APP_CONTEXT;
    asic_module_t *asic = &APP_CONTEXT.asic;

    // Initialize the ASIC module (creates semaphore, zeroes arrays)
    asic_module_init(asic, GLOBAL_STATE->asic_job_frequency_ms, GLOBAL_STATE->ASIC_difficulty);

    // Bridge: alias GlobalState pointers to module's arrays
    asic_module_bridge_legacy(asic, GLOBAL_STATE);

    ESP_LOGI(TAG, "ASIC Job Interval: %.2f ms", asic->job_interval_ms);
    SYSTEM_notify_mining_started(GLOBAL_STATE);
    ESP_LOGI(TAG, "ASIC Ready!");

    while (1)
    {
        bm_job *next_bm_job = (bm_job *)queue_dequeue(&APP_CONTEXT.ASIC_jobs_queue);

        ASIC_send_work(GLOBAL_STATE, next_bm_job);

        // Delay for ASIC(s) to finish the job
        xSemaphoreTake(asic->dispatch_semaphore, (asic->job_interval_ms / portTICK_PERIOD_MS));
    }
}
