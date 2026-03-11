#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "esp_app_desc.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"

#include "system.h"
#include "INA260.h"
#include "adc.h"
#include "connect.h"
#include "nvs_config.h"
#include "config.h"
#include "app_context.h"
#include "stratum_module.h"
#include "display.h"
#include "input.h"
#include "vcore.h"
#include "ThermalMonitoring.h"

static const char * TAG = "SystemModule";

static void _suffix_string(uint64_t, char *, size_t, int);

#define BLINK_GPIO_1 9
#define BLINK_GPIO_2 12

static esp_netif_t * netif;
static TimerHandle_t led_timer_1 = NULL;
static TimerHandle_t led_timer_2 = NULL;

//local function prototypes
static esp_err_t ensure_overheat_mode_config();

static void _suffix_string(uint64_t val, char * buf, size_t bufsiz, int sigdigits);

void SYSTEM_init_system(GlobalState * GLOBAL_STATE)
{
    extern app_context_t APP_CONTEXT;
    config_module_t *config = &APP_CONTEXT.config;

    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->duration_start = 0;
    module->historical_hashrate_rolling_index = 0;
    module->historical_hashrate_init = 0;
    module->current_hashrate = 0;
    module->shares_accepted = 0;
    module->shares_rejected = 0;
    module->best_nonce_diff = nvs_config_get_u64(NVS_CONFIG_BEST_DIFF, 0);
    module->best_session_nonce_diff = 0;
    module->start_time = esp_timer_get_time();
    module->lastClockSync = 0;
    module->FOUND_BLOCK = false;

    module->pool_url = config_get_string(config, NVS_CONFIG_STRATUM_URL, CONFIG_STRATUM_URL);
    module->fallback_pool_url = config_get_string(config, NVS_CONFIG_FALLBACK_STRATUM_URL, CONFIG_FALLBACK_STRATUM_URL);

    module->pool_port = config_get_u16(config, NVS_CONFIG_STRATUM_PORT, CONFIG_STRATUM_PORT);
    module->fallback_pool_port = config_get_u16(config, NVS_CONFIG_FALLBACK_STRATUM_PORT, CONFIG_FALLBACK_STRATUM_PORT);

    module->pool_user = config_get_string(config, NVS_CONFIG_STRATUM_USER, CONFIG_STRATUM_USER);
    module->fallback_pool_user = config_get_string(config, NVS_CONFIG_FALLBACK_STRATUM_USER, CONFIG_FALLBACK_STRATUM_USER);

    module->pool_pass = config_get_string(config, NVS_CONFIG_STRATUM_PASS, CONFIG_STRATUM_PW);
    module->fallback_pool_pass = config_get_string(config, NVS_CONFIG_FALLBACK_STRATUM_PASS, CONFIG_FALLBACK_STRATUM_PW);

    module->pool_suggested_difficulty = config_get_u16(config, NVS_CONFIG_STRATUM_DIFFICULTY, 0);
    module->fallback_pool_suggested_difficulty = config_get_u16(config, NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY, 0);
    module->pool_extranonce_subscribe = (bool)config_get_u16(config, NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE, 0);
    module->fallback_pool_extranonce_subscribe = (bool)config_get_u16(config, NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUB, 0);
    module->pool_decode_coinbase = (bool)config_get_u16(config, NVS_CONFIG_STRATUM_DECODE_COINBASE, 1);
    module->fallback_pool_decode_coinbase = (bool)config_get_u16(config, NVS_CONFIG_FALLBACK_STRATUM_DECODE_COINBASE, 1);

    module->is_using_fallback = config_get_u16(config, NVS_CONFIG_USE_FALLBACK_STRATUM, 0) != 0;
    module->response_time = 0.0f;
    module->share_submit_timestamp_us = 0;

    module->overheat_mode = config_get_u16(config, NVS_CONFIG_OVERHEAT_MODE, 0);
    ESP_LOGI(TAG, "Initial overheat_mode value: %d", module->overheat_mode);

    module->cpu0_percent = 0.0f;
    module->cpu1_percent = 0.0f;

    //Initialize power_fault fault mode
    module->power_fault = 0;

    // set the best diff string
    _suffix_string(module->best_nonce_diff, module->best_diff_string, DIFF_STRING_SIZE, 0);
    _suffix_string(module->best_session_nonce_diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);

    // set the ssid string to blank
    memset(module->ssid, 0, sizeof(module->ssid));

    // set the wifi_status to blank
    memset(module->wifi_status, 0, 20);

    // Initialize stratum module (Phase 5)
    pool_config_t primary_pool = {
        .url = module->pool_url,
        .port = module->pool_port,
        .username = module->pool_user,
        .password = module->pool_pass,
        .suggested_difficulty = module->pool_suggested_difficulty,
        .extranonce_subscribe = module->pool_extranonce_subscribe,
        .decode_coinbase = module->pool_decode_coinbase,
    };
    pool_config_t fallback_pool = {
        .url = module->fallback_pool_url,
        .port = module->fallback_pool_port,
        .username = module->fallback_pool_user,
        .password = module->fallback_pool_pass,
        .suggested_difficulty = module->fallback_pool_suggested_difficulty,
        .extranonce_subscribe = module->fallback_pool_extranonce_subscribe,
        .decode_coinbase = module->fallback_pool_decode_coinbase,
    };
    stratum_module_init(&APP_CONTEXT.stratum, &primary_pool, &fallback_pool);
    APP_CONTEXT.stratum.is_using_fallback = module->is_using_fallback;
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

esp_err_t SYSTEM_init_peripherals(GlobalState * GLOBAL_STATE) {
    
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "Error installing ISR service");
    ESP_RETURN_ON_ERROR(configure_led(), TAG, "LED config failed!");

    extern app_context_t APP_CONTEXT;
    config_module_t *config = &APP_CONTEXT.config;

    DeviceModel dm = GLOBAL_STATE->device_model;

    // Initialize the core voltage regulator
    ESP_RETURN_ON_ERROR(VCORE_init(dm), TAG, "VCORE init failed!");
    ESP_RETURN_ON_ERROR(VCORE_set_voltage(config_get_u16(config, NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE) / 1000.0, dm), TAG, "VCORE set voltage failed!");
    ESP_RETURN_ON_ERROR(Thermal_init(dm, GLOBAL_STATE->ASIC_initalized), TAG, "Thermal init failed!");
    ESP_RETURN_ON_ERROR(VCORE_set_voltage(0U, dm), TAG, "VCORE set voltage failed!");
    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Ensure overheat_mode config exists
    ESP_RETURN_ON_ERROR(ensure_overheat_mode_config(), TAG, "Failed to ensure overheat_mode config");

    int16_t voltage = VCORE_get_voltage_mv(dm);
    ESP_LOGI(TAG, "VCORE: %d", voltage);

    netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

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

void SYSTEM_notify_mining_started(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->duration_start = esp_timer_get_time();
}


/* Convert a uint64_t value into a truncated string for displaying with its
 * associated suitable for Mega, Giga etc. Buf array needs to be long enough */
static void _suffix_string(uint64_t val, char * buf, size_t bufsiz, int sigdigits)
{
    const double dkilo = 1000.0;
    const uint64_t kilo = 1000ull;
    const uint64_t mega = 1000000ull;
    const uint64_t giga = 1000000000ull;
    const uint64_t tera = 1000000000000ull;
    const uint64_t peta = 1000000000000000ull;
    const uint64_t exa = 1000000000000000000ull;
    char suffix[2] = "";
    bool decimal = true;
    double dval;

    if (val >= exa) {
        val /= peta;
        dval = (double) val / dkilo;
        strcpy(suffix, "E");
    } else if (val >= peta) {
        val /= tera;
        dval = (double) val / dkilo;
        strcpy(suffix, "P");
    } else if (val >= tera) {
        val /= giga;
        dval = (double) val / dkilo;
        strcpy(suffix, "T");
    } else if (val >= giga) {
        val /= mega;
        dval = (double) val / dkilo;
        strcpy(suffix, "G");
    } else if (val >= mega) {
        val /= kilo;
        dval = (double) val / dkilo;
        strcpy(suffix, "M");
    } else if (val >= kilo) {
        dval = (double) val / dkilo;
        strcpy(suffix, "k");
    } else {
        dval = val;
        decimal = false;
    }

    if (!sigdigits) {
        if (decimal)
            snprintf(buf, bufsiz, "%.3g%s", dval, suffix);
        else
            snprintf(buf, bufsiz, "%d%s", (unsigned int) dval, suffix);
    } else {
        /* Always show sigdigits + 1, padded on right with zeroes
         * followed by suffix */
        int ndigits = sigdigits - 1 - (dval > 0.0 ? floor(log10(dval)) : 0);

        snprintf(buf, bufsiz, "%*.*f%s", sigdigits + 1, ndigits, dval, suffix);
    }
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
