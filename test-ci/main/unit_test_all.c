#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_system.h"
#include "esp_rom_serial_output.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void print_banner(const char *text);

void app_main(void)
{
    print_banner("Running all the registered tests");
    UNITY_BEGIN();
    unity_run_tests_by_tag("[not-on-qemu]", true);
    UNITY_END();

    // Ensure all output reaches QEMU's serial file backend:
    // 1. Flush C stdio buffers
    fflush(stdout);
    fflush(stderr);
    // 2. Wait for UART TX FIFO to drain
    esp_rom_output_tx_wait_idle(0);
    // 3. Give QEMU time to flush its file backend
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Trigger CPU reset — QEMU's -no-reboot flag will exit cleanly
    esp_restart();
}

static void print_banner(const char *text)
{
    printf("\n#### %s #####\n\n", text);
}