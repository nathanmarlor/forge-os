#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "system.h"
#include "nvs_config.h"
#include "config.h"
#include "app_context.h"
#include "stratum_module.h"
#include "vcore.h"
#include "ThermalMonitoring.h"

static const char * TAG = "SystemModule";

#define BLINK_GPIO_1 9
#define BLINK_GPIO_2 12

static TimerHandle_t led_timer_1 = NULL;
static TimerHandle_t led_timer_2 = NULL;

//local function prototypes
static esp_err_t ensure_overheat_mode_config();

void SYSTEM_init_system(void)
{
    extern app_context_t APP_CONTEXT;
    config_module_t *config = &APP_CONTEXT.config;

    // Initialize stratum module from config
    pool_config_t primary_pool = {
        .url = config_get_string(config, NVS_CONFIG_STRATUM_URL, CONFIG_STRATUM_URL),
        .port = config_get_u16(config, NVS_CONFIG_STRATUM_PORT, CONFIG_STRATUM_PORT),
        .username = config_get_string(config, NVS_CONFIG_STRATUM_USER, CONFIG_STRATUM_USER),
        .password = config_get_string(config, NVS_CONFIG_STRATUM_PASS, CONFIG_STRATUM_PW),
        .suggested_difficulty = config_get_u16(config, NVS_CONFIG_STRATUM_DIFFICULTY, 0),
        .extranonce_subscribe = (bool)config_get_u16(config, NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE, 0),
        .decode_coinbase = (bool)config_get_u16(config, NVS_CONFIG_STRATUM_DECODE_COINBASE, 1),
    };
    pool_config_t fallback_pool = {
        .url = config_get_string(config, NVS_CONFIG_FALLBACK_STRATUM_URL, CONFIG_FALLBACK_STRATUM_URL),
        .port = config_get_u16(config, NVS_CONFIG_FALLBACK_STRATUM_PORT, CONFIG_FALLBACK_STRATUM_PORT),
        .username = config_get_string(config, NVS_CONFIG_FALLBACK_STRATUM_USER, CONFIG_FALLBACK_STRATUM_USER),
        .password = config_get_string(config, NVS_CONFIG_FALLBACK_STRATUM_PASS, CONFIG_FALLBACK_STRATUM_PW),
        .suggested_difficulty = config_get_u16(config, NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY, 0),
        .extranonce_subscribe = (bool)config_get_u16(config, NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUB, 0),
        .decode_coinbase = (bool)config_get_u16(config, NVS_CONFIG_FALLBACK_STRATUM_DECODE_COINBASE, 1),
    };
    stratum_module_init(&APP_CONTEXT.stratum, &primary_pool, &fallback_pool);
    APP_CONTEXT.stratum.is_using_fallback = config_get_u16(config, NVS_CONFIG_USE_FALLBACK_STRATUM, 0) != 0;

    ESP_LOGI(TAG, "Initial overheat_mode value: %d",
             config_get_u16(config, NVS_CONFIG_OVERHEAT_MODE, 0));
}

static void led_timer_callback(TimerHandle_t xTimer)
{
    if (xTimer == led_timer_1) {
        gpio_set_level(BLINK_GPIO_1, 1);
    } else if (xTimer == led_timer_2) {
        gpio_set_level(BLINK_GPIO_2, 1);
    }
}

static esp_err_t configure_led(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << BLINK_GPIO_1) | (1ULL << BLINK_GPIO_2);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_set_level(BLINK_GPIO_1, 1);
    gpio_set_level(BLINK_GPIO_2, 1);

    led_timer_1 = xTimerCreate("LED1", pdMS_TO_TICKS(300), pdFALSE, (void*)0, led_timer_callback);
    led_timer_2 = xTimerCreate("LED2", pdMS_TO_TICKS(300), pdFALSE, (void*)0, led_timer_callback);

    return ESP_OK;
}

esp_err_t SYSTEM_init_peripherals(void) {

    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "Error installing ISR service");
    ESP_RETURN_ON_ERROR(configure_led(), TAG, "LED config failed!");

    extern app_context_t APP_CONTEXT;
    config_module_t *config = &APP_CONTEXT.config;
    DeviceModel dm = APP_CONTEXT.device_model;

    // Initialize the core voltage regulator
    ESP_RETURN_ON_ERROR(VCORE_init(dm), TAG, "VCORE init failed!");
    ESP_RETURN_ON_ERROR(VCORE_set_voltage(config_get_u16(config, NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE) / 1000.0, dm), TAG, "VCORE set voltage failed!");
    ESP_RETURN_ON_ERROR(Thermal_init(dm, APP_CONTEXT.asic_initialized), TAG, "Thermal init failed!");
    ESP_RETURN_ON_ERROR(VCORE_set_voltage(0U, dm), TAG, "VCORE set voltage failed!");
    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Ensure overheat_mode config exists
    ESP_RETURN_ON_ERROR(ensure_overheat_mode_config(), TAG, "Failed to ensure overheat_mode config");

    int16_t voltage = VCORE_get_voltage_mv(dm);
    ESP_LOGI(TAG, "VCORE: %d", voltage);

    return ESP_OK;
}

static void switch_led(int num, uint8_t state)
{
    switch(num)
    {
        case 1:
            gpio_set_level(BLINK_GPIO_1, !state);
            if (state && led_timer_1) {
                xTimerReset(led_timer_1, 0);
            }
            break;
        case 2:
            gpio_set_level(BLINK_GPIO_2, !state);
            if (state && led_timer_2) {
                xTimerReset(led_timer_2, 0);
            }
            break;
        default:
            break;
    }
}

void SYSTEM_led_blink(int num)
{
    switch_led(num, 1);
}

static esp_err_t ensure_overheat_mode_config() {
    extern app_context_t APP_CONTEXT;
    config_module_t *config = &APP_CONTEXT.config;

    uint16_t overheat_mode = config_get_u16(config, NVS_CONFIG_OVERHEAT_MODE, UINT16_MAX);

    if (overheat_mode == UINT16_MAX) {
        config_set_u16(config, NVS_CONFIG_OVERHEAT_MODE, 0);
        ESP_LOGI(TAG, "Default value for overheat_mode set to 0");
    } else {
        ESP_LOGI(TAG, "Existing overheat_mode value: %d", overheat_mode);
    }

    return ESP_OK;
}
