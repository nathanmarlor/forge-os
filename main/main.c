
#include "esp_event.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "nvs_flash.h"

// #include "protocol_examples_common.h"
#include "main.h"

#include "asic_result_task.h"
#include "asic_task.h"
#include "create_jobs_task.h"
#include "hashrate_monitor_task.h"
#include "cpu_monitor_task.h"
#include "esp_netif.h"
#include "system.h"
#include "http_server.h"
#include "nvs_config.h"
#include "serial.h"
#include "stratum_task.h"
#include "i2c_bitforge.h"
#include "adc.h"
#include "nvs_device.h"
#include "self_test.h"
#include "asic.h"
#include "app_context.h"
#include "event_bus.h"

static GlobalState GLOBAL_STATE = {
    .extranonce_str = NULL,
    .extranonce_2_len = 0,
    .abandon_work = 0,
    .version_mask = 0,
    .ASIC_initalized = false
};

app_context_t APP_CONTEXT;

static const char * TAG = "bitforge";

static void ap_timeout_task(void * pvParameters)
{
    (void)pvParameters;

    // Wait 7 minutes (420 seconds)
    vTaskDelay(pdMS_TO_TICKS(420000));

    // Check if WiFi is still connected
    bool wifi_connected = (strlen(APP_CONTEXT.wifi.ip_addr_str) > 0 &&
                          strcmp(APP_CONTEXT.wifi.ip_addr_str, "0.0.0.0") != 0);

    if (wifi_connected && APP_CONTEXT.wifi.ap_enabled) {
        ESP_LOGI(TAG, "7 minutes elapsed and WiFi connected - turning off AP to save resources");
        wifi_softap_off();
    } else {
        ESP_LOGI(TAG, "7 minutes elapsed but WiFi not connected - keeping AP active");
    }

    // Task done, delete itself
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_reset_reason_t reset_reason = esp_reset_reason();
    const char * reset_reason_str;
    switch (reset_reason) {
        case ESP_RST_POWERON:   reset_reason_str = "Power On";                  break;
        case ESP_RST_EXT:       reset_reason_str = "External Pin Reset";        break;
        case ESP_RST_SW:        reset_reason_str = "Software Restart";          break;
        case ESP_RST_PANIC:     reset_reason_str = "Crash / Panic";             break;
        case ESP_RST_INT_WDT:   reset_reason_str = "Interrupt Watchdog";        break;
        case ESP_RST_TASK_WDT:  reset_reason_str = "Task Watchdog (hung task)"; break;
        case ESP_RST_WDT:       reset_reason_str = "Watchdog";                  break;
        case ESP_RST_DEEPSLEEP: reset_reason_str = "Deep Sleep Wakeup";         break;
        case ESP_RST_BROWNOUT:  reset_reason_str = "Brownout (low voltage)";    break;
        case ESP_RST_SDIO:      reset_reason_str = "SDIO Reset";                break;
        default:                reset_reason_str = "Unknown";                   break;
    }
    snprintf(GLOBAL_STATE.SYSTEM_MODULE.reset_reason, sizeof(GLOBAL_STATE.SYSTEM_MODULE.reset_reason),
             "%s", reset_reason_str);
    snprintf(APP_CONTEXT.reset_reason, sizeof(APP_CONTEXT.reset_reason),
             "%s", reset_reason_str);
    ESP_LOGW(TAG, "Reset reason: %s (%d)", reset_reason_str, reset_reason);

    ESP_LOGI(TAG, "Welcome to the bitforge nano || GTFO!");

    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "No PSRAM available on ESP32 device!");
        GLOBAL_STATE.psram_is_available = false;
    } else {
        GLOBAL_STATE.psram_is_available = true;
    }

    // Init I2C
    ESP_ERROR_CHECK(i2c_bitforge_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    //wait for I2C to init
    vTaskDelay(100 / portTICK_PERIOD_MS);

    //Init ADC
    ADC_init(GLOBAL_STATE.device_model);

    //initialize the ESP32 NVS
    if (NVSDevice_init() != ESP_OK){
        ESP_LOGE(TAG, "Failed to init NVS");
        return;
    }

    //parse the NVS config into GLOBAL_STATE
    if (NVSDevice_parse_config(&GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse NVS config");
        return;
    }

    if (ASIC_set_device_model(&GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "Error setting ASIC model");
        return;
    }

    // Initialize event bus and app context (Architecture V2)
    if (event_bus_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init event bus");
        return;
    }
    app_context_init_from_legacy(&APP_CONTEXT, &GLOBAL_STATE);

    // Initialize config module (Phase 2) - loads NVS cache, enables change events
    if (config_init(&APP_CONTEXT.config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init config module");
        return;
    }

    // Optionally hold the boot button
    // bool pressed = gpio_get_level(CONFIG_GPIO_BUTTON_BOOT) == 0; // LOW when pressed <--- Not suppoerted on Nano
    //should we run the self test?
    if (production_test(&GLOBAL_STATE)){ // || pressed) { // Button not supported
        execute_production_test((void *) &GLOBAL_STATE);
        return;
    }

    SYSTEM_init_system(&GLOBAL_STATE);

    char * wifi_ssid = config_get_string(&APP_CONTEXT.config, NVS_CONFIG_WIFI_SSID, WIFI_SSID);
    char * wifi_pass = config_get_string(&APP_CONTEXT.config, NVS_CONFIG_WIFI_PASS, WIFI_PASS);
    char * hostname  = config_get_string(&APP_CONTEXT.config, NVS_CONFIG_HOSTNAME, HOSTNAME);

    // copy the wifi ssid to the global state and app context
    strncpy(GLOBAL_STATE.SYSTEM_MODULE.ssid, wifi_ssid, sizeof(GLOBAL_STATE.SYSTEM_MODULE.ssid));
    GLOBAL_STATE.SYSTEM_MODULE.ssid[sizeof(GLOBAL_STATE.SYSTEM_MODULE.ssid)-1] = 0;
    strncpy(APP_CONTEXT.wifi.ssid, wifi_ssid, sizeof(APP_CONTEXT.wifi.ssid));
    APP_CONTEXT.wifi.ssid[sizeof(APP_CONTEXT.wifi.ssid)-1] = 0;

    // init AP and connect to wifi (writes IP into both buffers)
    wifi_init(wifi_ssid, wifi_pass, hostname, APP_CONTEXT.wifi.ip_addr_str);

    generate_ssid(APP_CONTEXT.wifi.ap_ssid);
    // Legacy sync
    strncpy(GLOBAL_STATE.SYSTEM_MODULE.ap_ssid, APP_CONTEXT.wifi.ap_ssid, sizeof(GLOBAL_STATE.SYSTEM_MODULE.ap_ssid));
    memcpy(GLOBAL_STATE.SYSTEM_MODULE.ip_addr_str, APP_CONTEXT.wifi.ip_addr_str, sizeof(GLOBAL_STATE.SYSTEM_MODULE.ip_addr_str));

    SYSTEM_init_peripherals(&GLOBAL_STATE);

    xTaskCreate(POWER_MANAGEMENT_task, "power management", 8192, (void *) &GLOBAL_STATE, 10, NULL);

    //start the API for AxeOS
    start_rest_server((void *) &GLOBAL_STATE);
    EventBits_t result_bits = wifi_connect();

    if (result_bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID: %s", wifi_ssid);
        strncpy(APP_CONTEXT.wifi.wifi_status, "Connected!", sizeof(APP_CONTEXT.wifi.wifi_status));
        strncpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, "Connected!", 20);
    } else if (result_bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", wifi_ssid);

        strncpy(APP_CONTEXT.wifi.wifi_status, "Failed to connect", sizeof(APP_CONTEXT.wifi.wifi_status));
        strncpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, "Failed to connect", 20);
        // User might be trying to configure with AP, just chill here
        ESP_LOGI(TAG, "Finished, waiting for user input.");
        while (1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        strncpy(APP_CONTEXT.wifi.wifi_status, "unexpected error", sizeof(APP_CONTEXT.wifi.wifi_status));
        strncpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, "unexpected error", 20);
        // User might be trying to configure with AP, just chill here
        ESP_LOGI(TAG, "Finished, waiting for user input.");
        while (1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    free(wifi_ssid);
    free(wifi_pass);
    free(hostname);

    GLOBAL_STATE.new_stratum_version_rolling_msg = false;

    // Keep AP active for dual-mode operation (AP + STA)
    // wifi_softap_off();  // Commented out to maintain AP access
    ESP_LOGI(TAG, "AP remains active for dual-mode operation");

    // Create task to turn off AP after 7 minutes if WiFi is connected
    xTaskCreate(&ap_timeout_task, "ap_timeout", 4096, NULL, 1, NULL);

    queue_init(&GLOBAL_STATE.stratum_queue);
    queue_init(&GLOBAL_STATE.ASIC_jobs_queue);
    queue_init(&APP_CONTEXT.stratum_queue);
    queue_init(&APP_CONTEXT.ASIC_jobs_queue);
    APP_CONTEXT.abandon_work = 0;

    SERIAL_init();

    if (ASIC_init(APP_CONTEXT.device_model, GLOBAL_STATE.POWER_MANAGEMENT_MODULE.frequency_value) == 0) {
        APP_CONTEXT.asic_status = "Chip count 0";
        GLOBAL_STATE.SYSTEM_MODULE.asic_status = "Chip count 0";
        ESP_LOGE(TAG, "Chip count 0");
        return;
    }

    SERIAL_set_baud(ASIC_set_max_baud(APP_CONTEXT.device_model));
    SERIAL_clear_buffer();

    GLOBAL_STATE.ASIC_initalized = true;
    APP_CONTEXT.asic_initialized = true;

    // Initialize stats module (Phase 3) - hashrate, shares, best diff, CPU
    int asic_count = ASIC_get_asic_count(APP_CONTEXT.device_model);
    if (stats_module_init(&APP_CONTEXT.stats, asic_count, STATS_MAX_HASH_DOMAINS) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init stats module");
        return;
    }

    xTaskCreate(stratum_task, "stratum admin", 8192, (void *) &GLOBAL_STATE, 5, NULL);
    xTaskCreate(create_jobs_task, "stratum miner", 8192, (void *) &GLOBAL_STATE, 10, NULL);
    xTaskCreate(ASIC_task, "asic", 8192, (void *) &GLOBAL_STATE, 10, NULL);
    xTaskCreate(ASIC_result_task, "asic result", 8192, (void *) &GLOBAL_STATE, 15, NULL);
    xTaskCreate(hashrate_monitor_task, "hashrate monitor", 4096, (void *) &GLOBAL_STATE, 5, NULL);
    xTaskCreate(cpu_monitor_task, "cpu monitor", 4096, (void *) &GLOBAL_STATE, 2, NULL);
}

void MINER_set_wifi_status(wifi_status_t status, int retry_count, int reason)
{
    char buf[20] = {0};
    switch(status) {
        case WIFI_CONNECTING:
            snprintf(buf, sizeof(buf), "Connecting...");
            break;
        case WIFI_CONNECTED:
            snprintf(buf, sizeof(buf), "Connected!");
            break;
        case WIFI_RETRYING:
            switch(reason) {
                case 201:
                    snprintf(buf, sizeof(buf), "No AP found (%d)", retry_count);
                    break;
                case 15:
                case 205:
                    snprintf(buf, sizeof(buf), "Password error (%d)", retry_count);
                    break;
                default:
                    snprintf(buf, sizeof(buf), "Error %d (%d)", reason, retry_count);
                    break;
            }
            break;
        default:
            ESP_LOGW(TAG, "Unknown status: %d", status);
            return;
    }
    memcpy(APP_CONTEXT.wifi.wifi_status, buf, sizeof(buf));
    memcpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, buf, sizeof(buf));
}

void MINER_set_ap_status(bool enabled) {
    APP_CONTEXT.wifi.ap_enabled = enabled;
    GLOBAL_STATE.SYSTEM_MODULE.ap_enabled = enabled;
}
