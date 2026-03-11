#ifndef STATS_MODULE_H_
#define STATS_MODULE_H_

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "common.h"

#define STATS_MAX_HASH_DOMAINS    4
#define STATS_MAX_REJECTION_REASONS 10
#define STATS_DIFF_STRING_SIZE    10

#ifndef MEASUREMENT_T_DEFINED
#define MEASUREMENT_T_DEFINED
typedef struct {
    uint32_t value;
    uint32_t time_ms;
    float hashrate;
} measurement_t;
#endif

typedef struct {
    char message[64];
    uint32_t count;
} rejected_reason_stat_t;

typedef struct {
    // ---- Hashrate (register-based) ----
    measurement_t *total_measurement;    // per-ASIC [asic_count]
    measurement_t **domain_measurements; // per-domain [hash_domains][asic_count]
    measurement_t *error_measurement;    // per-ASIC [asic_count]
    float hashrate;                      // EMA-smoothed total (GH/s)
    float error_percentage;
    int error_count;
    int asic_count;
    int hash_domains;
    bool hashrate_initialized;
    pthread_mutex_t measurement_lock;

    // ---- Shares (protected by share_lock) ----
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    rejected_reason_stat_t rejected_reasons[STATS_MAX_REJECTION_REASONS];
    int rejected_reason_count;
    pthread_mutex_t share_lock;

    // ---- Best difficulty (protected by share_lock) ----
    uint64_t best_nonce_diff;
    char best_diff_string[STATS_DIFF_STRING_SIZE];
    uint64_t best_session_nonce_diff;
    char best_session_diff_string[STATS_DIFF_STRING_SIZE];
    bool found_block;

    // ---- CPU ----
    float cpu0_percent;
    float cpu1_percent;

    // ---- Timing ----
    int64_t start_time;
    float frequency_value;   // tracks ASIC freq to reset measurements on change
} stats_module_t;

/**
 * Initialize the stats module. Allocates measurement arrays and
 * loads persisted best-diff from NVS.
 *
 * @param stats     Module instance (typically &APP_CONTEXT.stats)
 * @param asic_count Number of ASICs in the system
 * @param hash_domains Number of hash domains per ASIC (typically 4)
 * @return ESP_OK on success
 */
esp_err_t stats_module_init(stats_module_t *stats, int asic_count, int hash_domains);

/**
 * Process an ASIC register read. Called from the ASIC result task
 * when a register response arrives.
 */
void stats_handle_register_read(stats_module_t *stats, register_type_t register_type,
                                uint8_t asic_nr, uint32_t value);

/**
 * Compute the EMA hashrate and error percentage from current measurements.
 * Called periodically by the stats task after polling registers.
 */
void stats_compute_hashrate(stats_module_t *stats);

/**
 * Record an accepted share.
 */
void stats_notify_accepted_share(stats_module_t *stats);

/**
 * Record a rejected share with the pool's error message.
 */
void stats_notify_rejected_share(stats_module_t *stats, const char *error_msg);

/**
 * Check a found nonce's difficulty against session/all-time bests.
 * Persists new all-time best to NVS and checks for block finds.
 *
 * @param stats     Module instance
 * @param nonce_diff The difficulty of the found nonce
 * @param network_diff The current network difficulty (for block detection)
 */
void stats_check_best_diff(stats_module_t *stats, double nonce_diff, double network_diff);

/**
 * Update CPU load percentages. Called by the CPU monitor loop.
 */
void stats_update_cpu_load(stats_module_t *stats, float cpu0, float cpu1);

/**
 * Format a uint64_t difficulty value into a human-readable string
 * with metric suffix (k, M, G, T, P, E).
 */
void stats_format_diff_string(uint64_t val, char *buf, size_t bufsiz);

/**
 * Convert a hash counter delta and duration to GH/s.
 */
float stats_hash_counter_to_ghs(uint32_t duration_ms, uint32_t counter);

/**
 * Reset all hashrate measurements (e.g. on frequency change).
 */
void stats_reset_measurements(stats_module_t *stats);

#endif /* STATS_MODULE_H_ */
