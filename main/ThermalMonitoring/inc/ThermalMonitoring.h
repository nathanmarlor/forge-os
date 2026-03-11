#ifndef THERMAL_H
#define THERMAL_H

#include "device_model.h"
#include "esp_err.h"

// Debug for Thermal readouts
// #define DEBUG_THERMALMONITORING // Uncomment this line to enable debug logging

esp_err_t Thermal_init(DeviceModel device_model, bool asic_initialized);
esp_err_t Thermal_setFanSpeedPercent(float percent);
uint16_t Thermal_getFanSpeed(void);
float Thermal_getAsicChipTemp(bool asic_initialized);


#endif // THERMAL_H
