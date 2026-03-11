#include <string.h>
#include "INA260.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "global_state.h"
#include "math.h"
#include "mining.h"
#include "nvs_config.h"
#include "config.h"
#include "event_bus.h"
#include "app_context.h"
#include "power_module.h"
#include "serial.h"
#include "TPS546.h"
#include "vcore.h"
#include "PAC9544.h"
#include "ThermalMonitoring.h"
#include "power.h"
#include "asic.h"
#include "adc.h"

#define POLL_RATE 2000
#define MAX_TEMP 90.0
#define THROTTLE_TEMP 75.0
#define THROTTLE_TEMP_RANGE (MAX_TEMP - THROTTLE_TEMP)

#define VOLTAGE_START_THROTTLE 4900
#define VOLTAGE_MIN_THROTTLE 3500
#define VOLTAGE_RANGE (VOLTAGE_START_THROTTLE - VOLTAGE_MIN_THROTTLE)

#define TPS546_THROTTLE_TEMP 105.0
#define TPS546_MAX_TEMP 145.0

#define CONFIG_EVENT_QUEUE_SIZE 4

static const char * TAG = "power_management";

static bool even = false;

// Drain any pending config change events and apply them to local state.
// Returns true if any config relevant to power management changed.
static bool drain_config_events(QueueHandle_t config_queue, config_module_t *config,
                                uint16_t *core_voltage, uint16_t *asic_frequency)
{
    bool changed = false;
    event_t evt;
    while (xQueueReceive(config_queue, &evt, 0) == pdTRUE) {
        const char *key = evt.data.config.key;
        if (strcmp(key, NVS_CONFIG_ASIC_VOLTAGE) == 0 ||
            strcmp(key, NVS_CONFIG_ASIC_FREQ) == 0 ||
            strcmp(key, NVS_CONFIG_AUTO_FAN_SPEED) == 0 ||
            strcmp(key, NVS_CONFIG_FAN_SPEED) == 0 ||
            strcmp(key, NVS_CONFIG_FAN_TARGET_TEMP) == 0 ||
            strcmp(key, NVS_CONFIG_FAN_MIN_SPEED) == 0 ||
            strcmp(key, NVS_CONFIG_OVERHEAT_MODE) == 0) {
            changed = true;
        }
    }
    if (changed && config != NULL) {
        *core_voltage = config_get_u16(config, NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
        *asic_frequency = config_get_u16(config, NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);
    }
    return changed;
}

// Set the fan speed between min and 100% based on ASIC and VR temperatures.
// Uses the higher of the two temperature-based fan speed requirements.
static double automatic_fan_speed(float chip_temp, float vr_temp,
                                  power_module_t *pwr, config_module_t *config)
{
    double min_fan_speed = (double)config_get_u16(config, NVS_CONFIG_FAN_MIN_SPEED, 35);

    // Calculate fan speed based on ASIC temperature
    double asic_min_temp = (double)config_get_u16(config, NVS_CONFIG_FAN_TARGET_TEMP, 45);
    double asic_max_temp = THROTTLE_TEMP; // 75.0°C
    double asic_fan_speed = min_fan_speed;

    if (chip_temp < asic_min_temp) {
        asic_fan_speed = min_fan_speed;
    } else if (chip_temp >= asic_max_temp) {
        asic_fan_speed = 100.0;
    } else {
        double asic_temp_range = asic_max_temp - asic_min_temp;
        double fan_range = 100.0 - min_fan_speed;
        asic_fan_speed = ((chip_temp - asic_min_temp) / asic_temp_range) * fan_range + min_fan_speed;
    }

    // Calculate fan speed based on VR temperature
    double vr_min_temp = 60.0;
    double vr_max_temp = 85.0;
    double vr_fan_speed = min_fan_speed;

    if (vr_temp < vr_min_temp) {
        vr_fan_speed = min_fan_speed;
    } else if (vr_temp >= vr_max_temp) {
        vr_fan_speed = 100.0;
    } else {
        double vr_temp_range = vr_max_temp - vr_min_temp;
        double fan_range = 100.0 - min_fan_speed;
        vr_fan_speed = ((vr_temp - vr_min_temp) / vr_temp_range) * fan_range + min_fan_speed;
    }

    // Use the higher of the two calculated fan speeds
    double result = (asic_fan_speed > vr_fan_speed) ? asic_fan_speed : vr_fan_speed;

    #ifdef POWER_DEBUG
    const char* driver = (asic_fan_speed > vr_fan_speed) ? "ASIC" : "VR";
    ESP_LOGI(TAG, "Auto Fan: ASIC=%.1f°C(%.1f%%) VR=%.1f°C(%.1f%%) -> %.1f%% [%s]",
             chip_temp, asic_fan_speed, vr_temp, vr_fan_speed, result, driver);
    #endif

    pwr->fan_perc = result;
    Thermal_setFanSpeedPercent(result / 100.0);
    return result;
}

// Publish current temperature readings on the event bus
static void publish_temp_update(const power_module_t *pwr)
{
    event_t evt = event_create(EVT_TEMP_UPDATE);
    memcpy(evt.data.temperature.chip_temp, pwr->chip_temp, sizeof(pwr->chip_temp));
    evt.data.temperature.chip_temp_avg = pwr->chip_temp_avg;
    evt.data.temperature.vr_temp = pwr->vr_temp;
    event_bus_publish(&evt);
}

// Publish current power/fan readings on the event bus
static void publish_power_update(const power_module_t *pwr)
{
    event_t evt = event_create(EVT_POWER_UPDATE);
    evt.data.power.voltage = pwr->voltage;
    evt.data.power.power = pwr->power;
    evt.data.power.current = pwr->current;
    evt.data.power.fan_perc = pwr->fan_perc;
    memcpy(evt.data.power.fan_rpm, pwr->fan_rpm, sizeof(pwr->fan_rpm));
    event_bus_publish(&evt);
}

// Sync power module state to legacy GlobalState (dual-write during migration)
static void sync_to_legacy(const power_module_t *pwr, PowerManagementModule *legacy)
{
    legacy->voltage = pwr->voltage;
    legacy->power = pwr->power;
    legacy->current = pwr->current;
    legacy->vr_temp = pwr->vr_temp;
    legacy->fan_perc = pwr->fan_perc;
    memcpy(legacy->fan_rpm, pwr->fan_rpm, sizeof(pwr->fan_rpm));
    memcpy(legacy->chip_temp, pwr->chip_temp, sizeof(legacy->chip_temp));
    legacy->chip_temp_avg = pwr->chip_temp_avg;
    legacy->frequency_value = pwr->frequency_value;
    legacy->frequency_multiplier = pwr->frequency_multiplier;
}

void POWER_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    PowerManagementModule * legacy_pm = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;

    extern app_context_t APP_CONTEXT;
    config_module_t *config = &APP_CONTEXT.config;
    power_module_t *pwr = &APP_CONTEXT.power;

    // Initialize power module
    power_module_init(pwr);
    pwr->frequency_value = legacy_pm->frequency_value; // Preserve NVS-loaded value

    // Subscribe to config change events
    QueueHandle_t config_queue = xQueueCreate(CONFIG_EVENT_QUEUE_SIZE, sizeof(event_t));
    if (config_queue != NULL) {
        event_bus_subscribe(EVT_CONFIG_CHANGED, config_queue);
        ESP_LOGI(TAG, "Subscribed to EVT_CONFIG_CHANGED");
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);
    uint16_t last_core_voltage = 0;
    uint16_t last_asic_frequency = pwr->frequency_value;

    // Pre-load config values from cache
    uint16_t core_voltage = config_get_u16(config, NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
    uint16_t asic_frequency = config_get_u16(config, NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);

    while (1) {
        // Drain any pending config change events (non-blocking)
        if (config_queue != NULL) {
            drain_config_events(config_queue, config, &core_voltage, &asic_frequency);
        }

        PAC9544_selectChannel(even + 2U);
        vTaskDelay(pdMS_TO_TICKS(10)); // Allow PAC9544 channel switch to settle

        pwr->voltage = Power_get_input_voltage(GLOBAL_STATE);
        pwr->power = Power_get_power(GLOBAL_STATE);
        #ifdef POWER_DEBUG
        ESP_LOGI(TAG, "POWER: %f", pwr->power);
        #endif
        pwr->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);
        #ifdef POWER_DEBUG
        ESP_LOGI(TAG, "VCORE: %d", VCORE_get_voltage_mv(GLOBAL_STATE));
        #endif

        pwr->fan_rpm[even] = Thermal_getFanSpeed();

        PAC9544_selectChannel(2);
        vTaskDelay(pdMS_TO_TICKS(10));
        float temp_ASIC_1 = Thermal_getAsicChipTemp(GLOBAL_STATE);
        PAC9544_selectChannel(3);
        vTaskDelay(pdMS_TO_TICKS(10));
        float temp_ASIC_2 = Thermal_getAsicChipTemp(GLOBAL_STATE);

        pwr->chip_temp_avg = (temp_ASIC_1 + temp_ASIC_2) / 2;
        pwr->chip_temp[0] = temp_ASIC_1;
        pwr->chip_temp[1] = temp_ASIC_2;

        // Overheat protection
        bool asic1_overheat = (pwr->chip_temp[0] > 0.0f && pwr->chip_temp[0] > THROTTLE_TEMP);
        bool asic2_overheat = (pwr->chip_temp[1] > 0.0f && pwr->chip_temp[1] > THROTTLE_TEMP);
        bool vr_overheat = (pwr->vr_temp > TPS546_THROTTLE_TEMP);

        if ((vr_overheat || asic1_overheat || asic2_overheat) && (pwr->frequency_value > 50 || pwr->voltage > 1000)) {
            ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC1: %fC ASIC2: %fC", pwr->vr_temp, pwr->chip_temp[0], pwr->chip_temp[1]);
            pwr->fan_perc = 100;
            Thermal_setFanSpeedPercent(1);

            Power_disable(GLOBAL_STATE);

            config_set_u16(config, NVS_CONFIG_ASIC_VOLTAGE, 1000);
            config_set_u16(config, NVS_CONFIG_ASIC_FREQ, 50);
            config_set_u16(config, NVS_CONFIG_FAN_SPEED, 100);
            config_set_u16(config, NVS_CONFIG_AUTO_FAN_SPEED, 0);
            config_set_u16(config, NVS_CONFIG_OVERHEAT_MODE, 1);
            exit(EXIT_FAILURE);
        }

        if (config_get_u16(config, NVS_CONFIG_AUTO_FAN_SPEED, 1) == 1) {
            // Use the higher of the two valid ASIC temperatures for fan control
            float temp_for_fan = 0.0f;
            if (pwr->chip_temp[0] > 0.0f && pwr->chip_temp[1] > 0.0f) {
                temp_for_fan = (pwr->chip_temp[0] > pwr->chip_temp[1]) ?
                               pwr->chip_temp[0] : pwr->chip_temp[1];
            } else if (pwr->chip_temp[0] > 0.0f) {
                temp_for_fan = pwr->chip_temp[0];
            } else if (pwr->chip_temp[1] > 0.0f) {
                temp_for_fan = pwr->chip_temp[1];
            } else {
                temp_for_fan = 50.0f;
            }

            pwr->fan_perc = (float)automatic_fan_speed(temp_for_fan, pwr->vr_temp, pwr, config);

        } else {
            float fs = (float) config_get_u16(config, NVS_CONFIG_FAN_SPEED, 100);
            pwr->fan_perc = fs;
            Thermal_setFanSpeedPercent((float) fs / 100.0);
        }

        if (core_voltage != last_core_voltage) {
            ESP_LOGI(TAG, "setting new vcore voltage to %umV", core_voltage);
            VCORE_set_voltage((double) core_voltage / 1000.0, GLOBAL_STATE);
            last_core_voltage = core_voltage;
        }

        if (asic_frequency != last_asic_frequency) {
            ESP_LOGI(TAG, "New ASIC frequency requested: %uMHz (current: %uMHz)", asic_frequency, last_asic_frequency);

            bool success = ASIC_set_frequency(GLOBAL_STATE, (float)asic_frequency);

            if (success) {
                pwr->frequency_value = (float)asic_frequency;
            }

            last_asic_frequency = asic_frequency;
        }

        // Check for changing of overheat mode
        uint16_t new_overheat_mode = config_get_u16(config, NVS_CONFIG_OVERHEAT_MODE, 0);
        if (new_overheat_mode != pwr->overheat_mode) {
            pwr->overheat_mode = new_overheat_mode;
            sys_module->overheat_mode = new_overheat_mode; // Legacy sync
            ESP_LOGI(TAG, "Overheat mode updated to: %d", pwr->overheat_mode);
        }

        VCORE_check_fault(GLOBAL_STATE);

        // Publish events
        publish_temp_update(pwr);
        publish_power_update(pwr);

        // Dual-write to legacy GlobalState during migration
        sync_to_legacy(pwr, legacy_pm);

        even = !even;
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}
