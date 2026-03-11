#ifndef APP_CONTEXT_H_
#define APP_CONTEXT_H_

/**
 * Application context - the new top-level container for forge-os.
 *
 * Migration strategy:
 *   Phase 1 (current): app_context_t wraps the existing GlobalState.
 *                       Modules can access either the new context or
 *                       the legacy state during the transition.
 *   Phase 2+:           Modules are extracted one by one. Each module's
 *                       state moves out of GlobalState into a module struct.
 *   Final:              GlobalState is removed entirely.
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

    // ---- Legacy bridge ----
    // During migration, modules that haven't been extracted yet
    // continue to use GLOBAL_STATE through this pointer.
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
    ctx->legacy = gs;
}

#endif /* APP_CONTEXT_H_ */
