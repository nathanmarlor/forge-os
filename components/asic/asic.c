#include <string.h>

#include <esp_log.h>

#include "bm1370.h"

#include "asic.h"

static const char *TAG = "asic";

uint8_t ASIC_init(DeviceModel device_model, float initial_frequency) {
    switch (device_model) {
        case BITFORGE_NANO:
            return BM1370_init(initial_frequency, BITFORGE_NANO_ASIC_COUNT);
        default:
    }
    return ESP_OK;
}

uint8_t ASIC_get_asic_count(DeviceModel device_model) {
    switch (device_model) {
        case BITFORGE_NANO:
            return BITFORGE_NANO_ASIC_COUNT;
        default:
    }
    return 0;
}

uint16_t ASIC_get_small_core_count(DeviceModel device_model) {
    switch (device_model) {
        case BITFORGE_NANO:
            return BM1370_SMALL_CORE_COUNT;
        default:
    }
    return 0;
}

// .receive_result_fn = BM1366_process_work,
task_result * ASIC_process_work(DeviceModel device_model, asic_module_t *asic) {
    switch (device_model) {
        case BITFORGE_NANO:
            return BM1370_process_work(asic);
        default:
    }
    return NULL;
}

// .set_max_baud_fn = BM1366_set_max_baud,
int ASIC_set_max_baud(DeviceModel device_model) {
    switch (device_model) {
        case BITFORGE_NANO:
            return BM1370_set_max_baud();
        default:
    return 0;
    }
}

// .set_difficulty_mask_fn = BM1366_set_job_difficulty_mask,
void ASIC_set_job_difficulty_mask(DeviceModel device_model, uint8_t mask) {
    switch (device_model) {
        case BITFORGE_NANO:
            BM1370_set_job_difficulty_mask(mask);
            break;
        default:
    }
}

// .send_work_fn = BM1366_send_work,
void ASIC_send_work(DeviceModel device_model, asic_module_t *asic, bm_job *next_job) {
    switch (device_model) {
        case BITFORGE_NANO:
            BM1370_send_work(asic, next_job);
            break;
        default:
    return;
    }
}

// .set_version_mask = BM1366_set_version_mask
void ASIC_set_version_mask(DeviceModel device_model, uint32_t mask) {
    switch (device_model) {
        case BITFORGE_NANO:
            BM1370_set_version_mask(mask);
            break;
        default:
    return;
    }
}

bool ASIC_set_frequency(AsicModel asic_model, float target_frequency) {
    ESP_LOGI(TAG, "Setting ASIC frequency to %.2f MHz", target_frequency);
    bool success = false;

    switch (asic_model) {
        case ASIC_BM1370:
            success = BM1370_set_frequency(target_frequency);
            break;
        default:
            ESP_LOGE(TAG, "Unknown ASIC model, cannot set frequency");
            success = false;
            break;
    }

    if (success) {
        ESP_LOGI(TAG, "Successfully transitioned to new ASIC frequency: %.2f MHz", target_frequency);
    } else {
        ESP_LOGE(TAG, "Failed to transition to new ASIC frequency: %.2f MHz", target_frequency);
    }

    return success;
}

esp_err_t ASIC_set_device_model(GlobalState * GLOBAL_STATE) {

    if (GLOBAL_STATE->device_model_str == NULL) {
        ESP_LOGE(TAG, "No device model string found");
        return ESP_FAIL;
    }

    if (strcmp(GLOBAL_STATE->device_model_str, "BITFORGE_NANO") == 0) {
        GLOBAL_STATE->asic_model = ASIC_BM1370;
        //GLOBAL_STATE.asic_job_frequency_ms = (NONCE_SPACE / (double) (GLOBAL_STATE.POWER_MANAGEMENT_MODULE.frequency_value * BM1370_CORE_COUNT * 1000)) / (double) BITAXE_GAMMA_ASIC_COUNT; // version-rolling so Small Cores have different Nonce Space
        GLOBAL_STATE->asic_job_frequency_ms = 500; //ms
        GLOBAL_STATE->ASIC_difficulty = BM1370_ASIC_DIFFICULTY;
        ESP_LOGI(TAG, "DEVICE: BitForgeNano");
        ESP_LOGI(TAG, "ASIC: %dx BM1370 (%" PRIu64 " cores)", BITFORGE_NANO_ASIC_COUNT, BM1370_CORE_COUNT);
        GLOBAL_STATE->device_model = BITFORGE_NANO;

    } else {
        ESP_LOGE(TAG, "Invalid DEVICE model");
        GLOBAL_STATE->device_model = DEVICE_UNKNOWN;
        return ESP_FAIL;
    }
    return ESP_OK;
}
