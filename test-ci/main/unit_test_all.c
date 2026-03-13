#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void print_banner(const char *text);

void app_main(void)
{
    print_banner("Running all the registered tests");
    UNITY_BEGIN();
    unity_run_tests_by_tag("[not-on-qemu]", true);
    UNITY_END();

    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(3000));

    esp_restart();
}

static void print_banner(const char *text)
{
    printf("\n#### %s #####\n\n", text);
}