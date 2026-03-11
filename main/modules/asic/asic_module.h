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

    // Mutex protecting active_jobs and valid_jobs
    pthread_mutex_t jobs_mutex;
    pthread_mutex_t *jobs_lock;  // Points to jobs_mutex after init

    // Dispatch timing
    SemaphoreHandle_t dispatch_semaphore;
    double job_interval_ms;

    // ASIC config
    uint32_t asic_difficulty;

    bool initialized;
} asic_module_t;

/**
 * Initialize the ASIC module.
 * Creates semaphore, mutex, zeroes job arrays.
 */
void asic_module_init(asic_module_t *module, double job_interval_ms, uint32_t asic_difficulty);

#endif /* ASIC_MODULE_H_ */
