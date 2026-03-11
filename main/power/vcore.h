#ifndef VCORE_H_
#define VCORE_H_

#include <stdint.h>
#include "esp_err.h"
#include "device_model.h"

esp_err_t VCORE_init(DeviceModel device_model);
esp_err_t VCORE_set_voltage(float core_voltage, DeviceModel device_model);
int16_t VCORE_get_voltage_mv(DeviceModel device_model);
esp_err_t VCORE_check_fault(DeviceModel device_model, uint8_t *power_fault);
const char* VCORE_get_fault_string(DeviceModel device_model);

#endif /* VCORE_H_ */
