#ifndef APP_CONTEXT_H_
#define APP_CONTEXT_H_

/**
 * Application context - the new top-level container for forge-os.
 */

#include <stdbool.h>
#include <stdint.h>
#include "global_state.h"
#include "event_bus.h"
#include "config.h"
#include "stats.h"
#include "power_module.h"
#include "stratum_module.h"
#include "asic_module.h"
#include "work_queue.h"

// ---- WiFi / Network state ----
typedef struct {
    char ssid[32];
    char wifi_status[20];
    char ip_addr_str[16];
    char ap_ssid[32];
    bool ap_enabled;
} wifi_state_t;

// ---- OTA / Firmware update state ----
typedef struct {
    bool is_updating;
    char filename[20];
    char status[20];
} ota_state_t;

// ---- Self-test state ----
typedef struct {
    bool active;
    char *message;
    bool result;
    bool finished;
} self_test_state_t;

typedef struct {
    // ---- Device identity (immutable after boot) ----
    DeviceModel device_model;
    char *device_model_str;
    AsicModel asic_model;
    char *asic_model_str;
    int board_version;
    uint8_t asic_count;
    uint16_t small_core_count;
    uint32_t asic_difficulty;
    double asic_job_frequency_ms;
    bool psram_available;
    bool asic_initialized;

    // ---- Work queues ----
    work_queue stratum_queue;
    work_queue ASIC_jobs_queue;
    int abandon_work;

    // ---- Module instances ----
    config_module_t config;
    stats_module_t stats;
    power_module_t power;
    stratum_module_t stratum;
    asic_module_t asic;

    // ---- System state ----
    wifi_state_t wifi;
    ota_state_t ota;
    self_test_state_t self_test;
    bool is_screen_active;
    char *asic_status;      // NULL when ASIC is OK, error string otherwise
    char reset_reason[32];

    // ---- Coinbase data (from stratum) ----
    coinbase_output_t coinbase_outputs[MAX_COINBASE_TX_OUTPUTS];
    int coinbase_output_count;
    uint64_t coinbase_value_total_satoshis;

    // ---- Legacy bridge ----
    GlobalState *legacy;

} app_context_t;

/**
 * Initialize the app context from the legacy GlobalState.
 * Copies immutable device info and sets the legacy bridge pointer.
 */
static inline void app_context_init_from_legacy(app_context_t *ctx, GlobalState *gs)
{
    ctx->device_model = gs->device_model;
    ctx->device_model_str = gs->device_model_str;
    ctx->asic_model = gs->asic_model;
    ctx->asic_model_str = gs->asic_model_str;
    ctx->board_version = gs->board_version;
    ctx->asic_count = 0; // Set after ASIC init
    ctx->small_core_count = 0;
    ctx->asic_difficulty = gs->ASIC_difficulty;
    ctx->asic_job_frequency_ms = gs->asic_job_frequency_ms;
    ctx->psram_available = gs->psram_is_available;
    ctx->asic_status = NULL;
    ctx->legacy = gs;
}

#endif /* APP_CONTEXT_H_ */
