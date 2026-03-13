#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void print_banner(const char *text);

void app_main(void)
{
    // Suppress verbose log output to prevent UART FIFO stall in QEMU.
    // Test PASS/FAIL lines use printf directly and are unaffected.
    esp_log_level_set("*", ESP_LOG_NONE);

    print_banner("Running all the registered tests");
    UNITY_BEGIN();
    unity_run_tests_by_tag("[not-on-qemu]", true);
    UNITY_END();

    // Flush and wait for UART to drain before reset
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(3000));

    esp_restart();
}

static void print_banner(const char *text)
{
    printf("\n#### %s #####\n\n", text);
}