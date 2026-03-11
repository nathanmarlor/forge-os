#include <stdio.h>
#include <math.h>
#include "esp_log.h"

#include "vcore.h"
#include "adc.h"
#include "DS4432U.h"
#include "TPS546.h"
#include "INA260.h"

#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE
#define GPIO_ASIC_RESET  CONFIG_GPIO_ASIC_RESET
#define GPIO_PLUG_SENSE  CONFIG_GPIO_PLUG_SENSE

static TPS546_CONFIG TPS546_CONFIG_NANO = {
    /* vin voltage */
    .TPS546_INIT_VIN_ON = 11,
    .TPS546_INIT_VIN_OFF = 10.5,
    .TPS546_INIT_VIN_UV_WARN_LIMIT = 10.8, //Set to 0 to ignore. TI Bug in this register
    .TPS546_INIT_VIN_OV_FAULT_LIMIT = 13.5,
    /* vout voltage */
    .TPS546_INIT_SCALE_LOOP = 0.25,
    .TPS546_INIT_VOUT_MIN = 1,
    .TPS546_INIT_VOUT_MAX = 2,
    .TPS546_INIT_VOUT_COMMAND = 1.2,
    /* iout current */
    .TPS546_INIT_IOUT_OC_WARN_LIMIT = 38.00, /* A */
    .TPS546_INIT_IOUT_OC_FAULT_LIMIT = 40/* A */
};

static const char *TAG = "vcore.c";

esp_err_t VCORE_init(DeviceModel device_model) {
    switch (device_model) {
        case BITFORGE_NANO:
            ESP_RETURN_ON_ERROR(INA260_init(), TAG, "INA260 init failed!");
            ESP_RETURN_ON_ERROR(TPS546_init(TPS546_CONFIG_NANO), TAG, "TPS546 init failed!");
            break;
        default:
    }

    return ESP_OK;
}

esp_err_t VCORE_set_voltage(float core_voltage, DeviceModel device_model)
{
    switch (device_model) {
        case BITFORGE_NANO:
                ESP_LOGI(TAG, "Set ASIC voltage = %.3fV", core_voltage);
                ESP_RETURN_ON_ERROR(TPS546_set_vout(core_voltage), TAG, "TPS546 set voltage failed!");
            break;
        default:
    }

    return ESP_OK;
}

int16_t VCORE_get_voltage_mv(DeviceModel device_model) {

    switch (device_model) {
        case BITFORGE_NANO:
            return ADC_get_vcore();
        default:
    }
    return -1;
}

esp_err_t VCORE_check_fault(DeviceModel device_model, uint8_t *power_fault) {

    switch (device_model) {
        case BITFORGE_NANO:
            ESP_RETURN_ON_ERROR(TPS546_check_status(power_fault), TAG, "TPS546 check status failed!");
            break;
        default:
    }
    return ESP_OK;
}

const char* VCORE_get_fault_string(DeviceModel device_model) {
    switch (device_model) {
        case BITFORGE_NANO:
            return TPS546_get_error_message();
            break;
        default:
    }
    return NULL;
}
