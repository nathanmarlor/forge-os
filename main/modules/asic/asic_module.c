#include "asic_module.h"
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
