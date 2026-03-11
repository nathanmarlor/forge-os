#ifndef POWER_MODULE_H_
#define POWER_MODULE_H_

#include <stdbool.h>
#include <stdint.h>

#define POWER_MAX_ASICS 6

typedef struct {
    // ---- Temperature readings ----
    float chip_temp[POWER_MAX_ASICS];
    float chip_temp_avg;
    float vr_temp;

    // ---- Power readings ----
    float voltage;       // Input voltage (V)
    float power;         // Input power (W)
    float current;       // Input current (A)

    // ---- Fan state ----
    uint16_t fan_perc;
    uint16_t fan_rpm[2];

    // ---- ASIC control state ----
    float frequency_value;
    float frequency_multiplier;

    // ---- Overheat ----
    uint16_t overheat_mode;

    // ---- Fault state ----
    uint8_t power_fault;

    bool initialized;
} power_module_t;

/**
 * Initialize the power module with default state.
 */
void power_module_init(power_module_t *module);

#endif /* POWER_MODULE_H_ */
