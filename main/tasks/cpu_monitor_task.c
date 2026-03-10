#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "global_state.h"

#define CPU_MONITOR_POLL_MS   5000
#define CPU_MONITOR_MAX_TASKS 32

static const char *TAG = "cpu_monitor";

void cpu_monitor_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;

    TaskStatus_t prev_stats[CPU_MONITOR_MAX_TASKS];
    TaskStatus_t curr_stats[CPU_MONITOR_MAX_TASKS];
    uint32_t prev_total = 0;
    uint32_t curr_total = 0;

    // Seed the baseline sample
    UBaseType_t task_count = uxTaskGetSystemState(prev_stats, CPU_MONITOR_MAX_TASKS, &prev_total);
    if (task_count == 0) {
        ESP_LOGE(TAG, "uxTaskGetSystemState returned 0 — CONFIG_FREERTOS_USE_TRACE_FACILITY not enabled?");
        vTaskDelete(NULL);
        return;
    }

    TickType_t wake_time = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(CPU_MONITOR_POLL_MS));

        task_count = uxTaskGetSystemState(curr_stats, CPU_MONITOR_MAX_TASKS, &curr_total);
        if (task_count == 0) {
            continue;
        }

        uint32_t total_delta = curr_total - prev_total;
        if (total_delta == 0) {
            memcpy(prev_stats, curr_stats, task_count * sizeof(TaskStatus_t));
            prev_total = curr_total;
            continue;
        }

        uint32_t idle0_delta = 0;
        uint32_t idle1_delta = 0;

        for (UBaseType_t i = 0; i < task_count; i++) {
            for (UBaseType_t j = 0; j < task_count; j++) {
                if (curr_stats[i].xHandle == prev_stats[j].xHandle) {
                    uint32_t delta = curr_stats[i].ulRunTimeCounter - prev_stats[j].ulRunTimeCounter;
                    if (strcmp(curr_stats[i].pcTaskName, "IDLE0") == 0) {
                        idle0_delta = delta;
                    } else if (strcmp(curr_stats[i].pcTaskName, "IDLE1") == 0) {
                        idle1_delta = delta;
                    }
                    break;
                }
            }
        }

        // total_delta is the sum across both cores; halve it for per-core baseline
        uint32_t per_core_total = total_delta / 2;
        if (per_core_total == 0) {
            memcpy(prev_stats, curr_stats, task_count * sizeof(TaskStatus_t));
            prev_total = curr_total;
            continue;
        }

        float cpu0 = (1.0f - (float)idle0_delta / (float)per_core_total) * 100.0f;
        float cpu1 = (1.0f - (float)idle1_delta / (float)per_core_total) * 100.0f;

        // Clamp to [0, 100]
        if (cpu0 < 0.0f)   cpu0 = 0.0f;
        if (cpu0 > 100.0f) cpu0 = 100.0f;
        if (cpu1 < 0.0f)   cpu1 = 0.0f;
        if (cpu1 > 100.0f) cpu1 = 100.0f;

        module->cpu0_percent = cpu0;
        module->cpu1_percent = cpu1;

        ESP_LOGD(TAG, "CPU load — Core0: %.1f%%, Core1: %.1f%%", cpu0, cpu1);

        memcpy(prev_stats, curr_stats, task_count * sizeof(TaskStatus_t));
        prev_total = curr_total;
    }
}
