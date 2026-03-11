#include "asic_module.h"
#include "global_state.h"
#include <string.h>

void asic_module_init(asic_module_t *module, double job_interval_ms, uint32_t asic_difficulty)
{
    memset(module->active_jobs, 0, sizeof(module->active_jobs));
    memset(module->valid_jobs, 0, sizeof(module->valid_jobs));
    module->dispatch_semaphore = xSemaphoreCreateBinary();
    module->job_interval_ms = job_interval_ms;
    module->asic_difficulty = asic_difficulty;

    // Initialize the owned mutex and point jobs_lock to it
    pthread_mutex_init(&module->jobs_mutex, NULL);
    module->jobs_lock = &module->jobs_mutex;

    module->initialized = true;
}

void asic_module_bridge_legacy(asic_module_t *module, void *global_state)
{
    GlobalState *gs = (GlobalState *)global_state;

    // Override: use GlobalState's mutex (self_test frees arrays via GlobalState pointers)
    pthread_mutex_init(&gs->valid_jobs_lock, NULL);
    module->jobs_lock = &gs->valid_jobs_lock;

    // Alias: GlobalState pointers reference the module's arrays
    gs->valid_jobs = module->valid_jobs;
    gs->ASIC_TASK_MODULE.active_jobs = module->active_jobs;
    gs->ASIC_TASK_MODULE.semaphore = module->dispatch_semaphore;
}
