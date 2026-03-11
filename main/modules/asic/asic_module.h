#ifndef ASIC_MODULE_H_
#define ASIC_MODULE_H_

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mining.h"

#define ASIC_JOB_SLOTS 128

typedef struct {
    // Active job table (owns the 128-slot arrays)
    bm_job *active_jobs[ASIC_JOB_SLOTS];
    uint8_t valid_jobs[ASIC_JOB_SLOTS];

    // Pointer to the jobs mutex. During migration, points into GlobalState.
    // When GlobalState is removed, embed a pthread_mutex_t here instead.
    pthread_mutex_t *jobs_lock;

    // Dispatch timing
    SemaphoreHandle_t dispatch_semaphore;
    double job_interval_ms;

    // ASIC config
    uint32_t asic_difficulty;

    bool initialized;
} asic_module_t;

/**
 * Initialize the ASIC module.
 * Creates semaphore, zeroes job arrays.
 * The jobs_lock pointer must be set separately via bridge_legacy.
 */
void asic_module_init(asic_module_t *module, double job_interval_ms, uint32_t asic_difficulty);

/**
 * Wire legacy GlobalState pointers to alias the module's arrays.
 * Also sets the module's jobs_lock pointer to GlobalState's mutex.
 */
void asic_module_bridge_legacy(asic_module_t *module, void *global_state);

#endif /* ASIC_MODULE_H_ */
