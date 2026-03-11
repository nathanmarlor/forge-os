#include "stats.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <inttypes.h>
#include <esp_heap_caps.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_config.h"

#define EMA_ALPHA 12
#define HASH_CNT_LSB 0x100000000uLL  // 2^32 hashes per counter increment
#define HASHRATE_UNIT 0x100000uLL    // 2^24 hashes per hashrate register unit

static const char *TAG = "stats";

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static float sum_hashrates(measurement_t *measurement, int asic_count)
{
    if (asic_count == 1) return measurement[0].hashrate;
    float total = 0;
    for (int i = 0; i < asic_count; i++) {
        total += measurement[i].hashrate;
    }
    return total;
}

static uint32_t sum_values(measurement_t *measurement, int asic_count)
{
    if (asic_count == 1) return measurement[0].value;
    uint32_t total = 0;
    for (int i = 0; i < asic_count; i++) total += measurement[i].value;
    return total;
}

static void clear_measurements(stats_module_t *stats)
{
    memset(stats->total_measurement, 0, stats->asic_count * sizeof(measurement_t));
    if (stats->hash_domains > 0) {
        memset(stats->domain_measurements[0], 0,
               stats->asic_count * stats->hash_domains * sizeof(measurement_t));
    }
    memset(stats->error_measurement, 0, stats->asic_count * sizeof(measurement_t));
}

static void update_hashrate_register(uint32_t value, measurement_t *measurement, int asic_nr)
{
    uint8_t flag_long = (value & 0x80000000) >> 31;
    uint32_t hashrate_value = value & 0x7FFFFFFF;

    if (hashrate_value != 0x007FFFFF && !flag_long) {
        float hashrate = hashrate_value * (float)HASHRATE_UNIT;
        measurement[asic_nr].hashrate = hashrate / 1e9f;
    }
}

static void update_hash_counter(uint32_t time_ms, uint32_t value,
                                measurement_t *measurement, int asic_nr)
{
    uint32_t previous_time_ms = measurement[asic_nr].time_ms;
    if (previous_time_ms != 0) {
        uint32_t duration_ms = time_ms - previous_time_ms;
        uint32_t counter = value - measurement[asic_nr].value;
        measurement[asic_nr].hashrate = stats_hash_counter_to_ghs(duration_ms, counter);
    }
    measurement[asic_nr].value = value;
    measurement[asic_nr].time_ms = time_ms;
}

static int compare_rejected_reasons(const void *a, const void *b)
{
    const rejected_reason_stat_t *ea = a;
    const rejected_reason_stat_t *eb = b;
    return (eb->count > ea->count) - (ea->count > eb->count);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t stats_module_init(stats_module_t *stats, int asic_count, int hash_domains)
{
    memset(stats, 0, sizeof(*stats));
    stats->asic_count = asic_count;
    stats->hash_domains = hash_domains;
    stats->start_time = esp_timer_get_time();

    // Allocate per-ASIC total measurement (prefer SPIRAM)
    stats->total_measurement = heap_caps_malloc(asic_count * sizeof(measurement_t), MALLOC_CAP_SPIRAM);
    if (!stats->total_measurement) {
        stats->total_measurement = malloc(asic_count * sizeof(measurement_t));
    }
    if (!stats->total_measurement) {
        ESP_LOGE(TAG, "Failed to allocate total_measurement");
        return ESP_ERR_NO_MEM;
    }

    // Allocate per-domain measurements
    if (hash_domains > 0) {
        measurement_t *data = malloc(asic_count * hash_domains * sizeof(measurement_t));
        if (!data) {
            ESP_LOGE(TAG, "Failed to allocate domain measurement data");
            return ESP_ERR_NO_MEM;
        }
        stats->domain_measurements = heap_caps_malloc(hash_domains * sizeof(measurement_t *), MALLOC_CAP_SPIRAM);
        if (!stats->domain_measurements) {
            stats->domain_measurements = malloc(hash_domains * sizeof(measurement_t *));
        }
        if (!stats->domain_measurements) {
            free(data);
            ESP_LOGE(TAG, "Failed to allocate domain_measurements");
            return ESP_ERR_NO_MEM;
        }
        for (int i = 0; i < hash_domains; i++) {
            stats->domain_measurements[i] = data + (i * asic_count);
        }
    }

    // Allocate per-ASIC error measurement
    stats->error_measurement = heap_caps_malloc(asic_count * sizeof(measurement_t), MALLOC_CAP_SPIRAM);
    if (!stats->error_measurement) {
        stats->error_measurement = malloc(asic_count * sizeof(measurement_t));
    }
    if (!stats->error_measurement) {
        ESP_LOGE(TAG, "Failed to allocate error_measurement");
        return ESP_ERR_NO_MEM;
    }

    pthread_mutex_init(&stats->measurement_lock, NULL);
    clear_measurements(stats);

    // Load persisted best difficulty from NVS
    stats->best_nonce_diff = nvs_config_get_u64(NVS_CONFIG_BEST_DIFF, 0);
    stats_format_diff_string(stats->best_nonce_diff, stats->best_diff_string, STATS_DIFF_STRING_SIZE);

    stats->hashrate_initialized = true;
    ESP_LOGI(TAG, "Stats module initialized for %d ASICs, %d domains", asic_count, hash_domains);
    return ESP_OK;
}

void stats_handle_register_read(stats_module_t *stats, register_type_t register_type,
                                uint8_t asic_nr, uint32_t value)
{
    uint32_t time_ms = esp_timer_get_time() / 1000;

    if (asic_nr >= stats->asic_count) {
        ESP_LOGE(TAG, "ASIC nr %d out of bounds (max %d)", asic_nr, stats->asic_count);
        return;
    }

    pthread_mutex_lock(&stats->measurement_lock);

    switch (register_type) {
    case REGISTER_HASHRATE:
        update_hashrate_register(value, stats->total_measurement, asic_nr);
        break;
    case REGISTER_TOTAL_COUNT:
        update_hash_counter(time_ms, value, stats->total_measurement, asic_nr);
        break;
    case REGISTER_DOMAIN_0_COUNT:
        update_hash_counter(time_ms, value, stats->domain_measurements[0], asic_nr);
        break;
    case REGISTER_DOMAIN_1_COUNT:
        update_hash_counter(time_ms, value, stats->domain_measurements[1], asic_nr);
        break;
    case REGISTER_DOMAIN_2_COUNT:
        update_hash_counter(time_ms, value, stats->domain_measurements[2], asic_nr);
        break;
    case REGISTER_DOMAIN_3_COUNT:
        update_hash_counter(time_ms, value, stats->domain_measurements[3], asic_nr);
        break;
    case REGISTER_ERROR_COUNT:
        update_hash_counter(time_ms, value, stats->error_measurement, asic_nr);
        break;
    case REGISTER_INVALID:
        ESP_LOGE(TAG, "Invalid register type");
        break;
    }

    pthread_mutex_unlock(&stats->measurement_lock);
}

void stats_compute_hashrate(stats_module_t *stats)
{
    pthread_mutex_lock(&stats->measurement_lock);
    float hashrate = sum_hashrates(stats->total_measurement, stats->asic_count);
    float error_hashrate = sum_hashrates(stats->error_measurement, stats->asic_count);
    uint32_t error_count = sum_values(stats->error_measurement, stats->asic_count);
    pthread_mutex_unlock(&stats->measurement_lock);

    if (hashrate == 0.0f) {
        stats->hashrate = 0.0f;
    } else if (stats->hashrate == 0.0f) {
        stats->hashrate = hashrate;
    } else {
        stats->hashrate = ((stats->hashrate * (EMA_ALPHA - 1)) + hashrate) / EMA_ALPHA;
    }

    stats->error_percentage = stats->hashrate > 0
        ? error_hashrate / stats->hashrate * 100.0f
        : 0.0f;
    stats->error_count = error_count;
}

void stats_notify_accepted_share(stats_module_t *stats)
{
    stats->shares_accepted++;
}

void stats_notify_rejected_share(stats_module_t *stats, const char *error_msg)
{
    stats->shares_rejected++;

    // Try to find existing reason
    for (int i = 0; i < stats->rejected_reason_count; i++) {
        if (strncmp(stats->rejected_reasons[i].message, error_msg,
                    sizeof(stats->rejected_reasons[i].message) - 1) == 0) {
            stats->rejected_reasons[i].count++;
            return;
        }
    }

    // Add new reason if space available
    if (stats->rejected_reason_count < STATS_MAX_REJECTION_REASONS) {
        strncpy(stats->rejected_reasons[stats->rejected_reason_count].message,
                error_msg,
                sizeof(stats->rejected_reasons[stats->rejected_reason_count].message) - 1);
        stats->rejected_reasons[stats->rejected_reason_count].message[
            sizeof(stats->rejected_reasons[0].message) - 1] = '\0';
        stats->rejected_reasons[stats->rejected_reason_count].count = 1;
        stats->rejected_reason_count++;
    }

    if (stats->rejected_reason_count > 1) {
        qsort(stats->rejected_reasons, stats->rejected_reason_count,
              sizeof(stats->rejected_reasons[0]), compare_rejected_reasons);
    }
}

void stats_check_best_diff(stats_module_t *stats, double nonce_diff, double network_diff)
{
    uint64_t diff = (uint64_t)nonce_diff;

    if (diff > stats->best_session_nonce_diff) {
        stats->best_session_nonce_diff = diff;
        stats_format_diff_string(diff, stats->best_session_diff_string, STATS_DIFF_STRING_SIZE);
    }

    if (diff > stats->best_nonce_diff) {
        stats->best_nonce_diff = diff;
        nvs_config_set_u64(NVS_CONFIG_BEST_DIFF, stats->best_nonce_diff);
        stats_format_diff_string(diff, stats->best_diff_string, STATS_DIFF_STRING_SIZE);
    }

    if (nonce_diff > network_diff) {
        stats->found_block = true;
        ESP_LOGI(TAG, "FOUND BLOCK! %.0f > %.0f", nonce_diff, network_diff);
    }
}

void stats_update_cpu_load(stats_module_t *stats, float cpu0, float cpu1)
{
    stats->cpu0_percent = cpu0;
    stats->cpu1_percent = cpu1;
}

void stats_format_diff_string(uint64_t val, char *buf, size_t bufsiz)
{
    const double dkilo = 1000.0;
    const uint64_t kilo = 1000ull;
    const uint64_t mega = 1000000ull;
    const uint64_t giga = 1000000000ull;
    const uint64_t tera = 1000000000000ull;
    const uint64_t peta = 1000000000000000ull;
    const uint64_t exa  = 1000000000000000000ull;
    char suffix[2] = "";
    bool decimal = true;
    double dval;

    if (val >= exa) {
        val /= peta;
        dval = (double)val / dkilo;
        strcpy(suffix, "E");
    } else if (val >= peta) {
        val /= tera;
        dval = (double)val / dkilo;
        strcpy(suffix, "P");
    } else if (val >= tera) {
        val /= giga;
        dval = (double)val / dkilo;
        strcpy(suffix, "T");
    } else if (val >= giga) {
        val /= mega;
        dval = (double)val / dkilo;
        strcpy(suffix, "G");
    } else if (val >= mega) {
        val /= kilo;
        dval = (double)val / dkilo;
        strcpy(suffix, "M");
    } else if (val >= kilo) {
        dval = (double)val / dkilo;
        strcpy(suffix, "k");
    } else {
        dval = val;
        decimal = false;
    }

    if (decimal) {
        snprintf(buf, bufsiz, "%.3g%s", dval, suffix);
    } else {
        snprintf(buf, bufsiz, "%d%s", (unsigned int)dval, suffix);
    }
}

float stats_hash_counter_to_ghs(uint32_t duration_ms, uint32_t counter)
{
    if (duration_ms == 0) return 0.0f;
    float seconds = duration_ms / 1000.0f;
    float hashrate = counter / seconds * (float)HASH_CNT_LSB;
    return hashrate / 1e9f;
}

void stats_reset_measurements(stats_module_t *stats)
{
    pthread_mutex_lock(&stats->measurement_lock);
    clear_measurements(stats);
    pthread_mutex_unlock(&stats->measurement_lock);
}
