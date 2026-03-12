#ifndef ASIC_H
#define ASIC_H

#include <esp_err.h>
#include "device_model.h"
#include "common.h"
#include "asic_module.h"

#define BITFORGE_NANO_ASIC_COUNT 2

typedef struct app_context_t app_context_t;  // forward declaration

uint8_t ASIC_init(DeviceModel device_model, float initial_frequency);
uint8_t ASIC_get_asic_count(DeviceModel device_model);
uint16_t ASIC_get_small_core_count(DeviceModel device_model);
task_result * ASIC_process_work(DeviceModel device_model, asic_module_t *asic);
int ASIC_set_max_baud(DeviceModel device_model);
void ASIC_set_job_difficulty_mask(DeviceModel device_model, int difficulty);
void ASIC_send_work(DeviceModel device_model, asic_module_t *asic, bm_job *next_job);
void ASIC_set_version_mask(DeviceModel device_model, uint32_t mask);
bool ASIC_set_frequency(AsicModel asic_model, float target_frequency);
esp_err_t ASIC_set_device_model(app_context_t *ctx);

#endif // ASIC_H
