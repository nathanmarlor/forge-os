#ifndef GLOBAL_STATE_H_
#define GLOBAL_STATE_H_

/*
 * Minimal mock of global_state.h for ASIC module tests.
 * Only defines the fields accessed by asic_module_bridge_legacy().
 */

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mining.h"

typedef struct {
    bm_job **active_jobs;
    SemaphoreHandle_t semaphore;
} AsicTaskModule;

typedef struct {
    uint8_t *valid_jobs;
    pthread_mutex_t valid_jobs_lock;
    AsicTaskModule ASIC_TASK_MODULE;
} GlobalState;

#endif /* GLOBAL_STATE_H_ */
