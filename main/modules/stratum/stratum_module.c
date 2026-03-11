#include "stratum_module.h"
#include "esp_timer.h"
#include <string.h>

void stratum_module_init(stratum_module_t *module,
                         const pool_config_t *primary,
                         const pool_config_t *fallback)
{
    memset(module, 0, sizeof(*module));

    // Copy pool configs (shallow copy - pointers owned by caller)
    if (primary) {
        module->primary = *primary;
    }
    if (fallback) {
        module->fallback = *fallback;
    }

    pthread_mutex_init(&module->connection_lock, NULL);
    module->stratum_difficulty = 8192; // Default
    module->initialized = true;
}

void stratum_rtt_start(stratum_module_t *module)
{
    module->rtt.last_submit_us = esp_timer_get_time();
}

void stratum_rtt_record(stratum_module_t *module)
{
    if (module->rtt.last_submit_us <= 0) return;

    float rtt = (esp_timer_get_time() - module->rtt.last_submit_us) / 1000.0f;
    module->rtt.last_submit_us = 0;

    // EMA (alpha=0.1)
    if (module->rtt.ema <= 0.0f) {
        module->rtt.ema = rtt;
    } else {
        module->rtt.ema = 0.9f * module->rtt.ema + 0.1f * rtt;
    }

    // Session min/max
    if (module->rtt.min <= 0.0f || rtt < module->rtt.min) {
        module->rtt.min = rtt;
    }
    if (rtt > module->rtt.max) {
        module->rtt.max = rtt;
    }

    // Circular buffer for percentile computation
    module->rtt.samples[module->rtt.sample_idx] = rtt;
    module->rtt.sample_idx = (module->rtt.sample_idx + 1) % STRATUM_RTT_SAMPLE_COUNT;
    if (module->rtt.sample_count < STRATUM_RTT_SAMPLE_COUNT) {
        module->rtt.sample_count++;
    }
}

const pool_config_t *stratum_get_active_pool(const stratum_module_t *module)
{
    return module->is_using_fallback ? &module->fallback : &module->primary;
}
