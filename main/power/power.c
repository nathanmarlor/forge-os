#include "TPS546.h"
#include "INA260.h"
#include "DS4432U.h"

#include "power.h"
#include "vcore.h"

#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE

#define SUPRA_POWER_OFFSET 5 //Watts
#define GAMMA_POWER_OFFSET 5 //Watts
#define GAMMATURBO_POWER_OFFSET 5 //Watts

// max power settings
#define BITFORGE_NANO_MAX_POWER 60 //Watts

// nominal voltage settings
#define NOMINAL_VOLTAGE_5 5 //volts
#define NOMINAL_VOLTAGE_12 12//volts

esp_err_t Power_disable(DeviceModel device_model) {

    switch (device_model) {
        case BITFORGE_NANO:
            // Turn off core voltage
            VCORE_set_voltage(0.0, device_model);
            break;
        default:
    }
    return ESP_OK;

}

float Power_get_max_settings(DeviceModel device_model) {

    switch (device_model) {
        case BITFORGE_NANO:
            return BITFORGE_NANO_MAX_POWER;
        default:
            return BITFORGE_NANO_MAX_POWER;
    }
}

float Power_get_current(DeviceModel device_model) {
    float current = 0.0;

    switch (device_model) {
        case BITFORGE_NANO:
            current = INA260_read_current();
            break;
        default:
    }

    return current;
}

float Power_get_power(DeviceModel device_model) {
    float power = 0.0;

    switch (device_model) {
        case BITFORGE_NANO:
            power = INA260_read_power() / 1000.0;
            break;
        default:
    }

    return power;
}


float Power_get_input_voltage(DeviceModel device_model) {

    switch (device_model) {
        case BITFORGE_NANO:
            return INA260_read_voltage();
            break;
        default:
    }

    return 0.0;
}

int Power_get_nominal_voltage(DeviceModel device_model) {
    switch (device_model)
    {
        case BITFORGE_NANO:
            return NOMINAL_VOLTAGE_12;
        default:
            return NOMINAL_VOLTAGE_12;
    }
}

float Power_get_vr_voltage(DeviceModel device_model) {

    switch (device_model) {
        case BITFORGE_NANO:
            return TPS546_get_vout();
        default:
    }

    return 0.0;
}

float Power_get_vr_current(DeviceModel device_model) {

    switch (device_model) {
        case BITFORGE_NANO:
            return TPS546_get_iout();
        default:
    }

    return 0.0;
}

float Power_get_vreg_temp(DeviceModel device_model) {

    switch (device_model) {
        case BITFORGE_NANO:
                return TPS546_get_temperature();
            break;
        default:
    }

    return 0.0;
}
