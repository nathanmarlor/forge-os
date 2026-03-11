#ifndef POWER_H
#define POWER_H

#include <esp_err.h>
#include "global_state.h"


esp_err_t Power_disable(DeviceModel device_model);

float Power_get_current(DeviceModel device_model);
float Power_get_vr_voltage(DeviceModel device_model);
float Power_get_vr_current(DeviceModel device_model);
float Power_get_power(DeviceModel device_model);
float Power_get_input_voltage(DeviceModel device_model);
float Power_get_vreg_temp(DeviceModel device_model);
float Power_get_max_settings(DeviceModel device_model);
int Power_get_nominal_voltage(DeviceModel device_model);

#endif // POWER_H
