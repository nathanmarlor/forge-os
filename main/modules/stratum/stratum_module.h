#ifndef STRATUM_MODULE_H_
#define STRATUM_MODULE_H_

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <pthread.h>

#define STRATUM_RTT_SAMPLE_COUNT 100

typedef struct {
    char *url;
    uint16_t port;
    char *username;
    char *password;
    uint16_t suggested_difficulty;
    bool extranonce_subscribe;
    bool decode_coinbase;
} pool_config_t;

typedef struct {
    float ema;                    // EMA-smoothed RTT (ms)
    float min;                    // Session minimum RTT (ms)
    float max;                    // Session maximum RTT (ms)
    float samples[STRATUM_RTT_SAMPLE_COUNT]; // Circular buffer for percentile
    uint8_t sample_idx;
    uint8_t sample_count;
    int64_t last_submit_us;       // Timestamp of last share submission
} rtt_tracker_t;

typedef struct {
    // Pool configuration
    pool_config_t primary;
    pool_config_t fallback;
    bool is_using_fallback;

    // Protocol state
    uint32_t stratum_difficulty;
    uint32_t version_mask;
    char *extranonce_str;
    int extranonce_2_len;
    bool new_version_rolling_msg;

    // Connection state
    int sock;
    atomic_int send_uid;
    pthread_mutex_t connection_lock;  // Protects sock, extranonce_str

    // Response time tracking
    rtt_tracker_t rtt;

    // Block info (from mining.notify)
    int block_height;
    char scriptsig[128];
    uint64_t network_nonce_diff;

    bool initialized;
} stratum_module_t;

/**
 * Initialize the stratum module with pool configuration.
 * Copies pool config from the provided pool_config_t structs.
 */
void stratum_module_init(stratum_module_t *module,
                         const pool_config_t *primary,
                         const pool_config_t *fallback);

/**
 * Record a share submission timestamp for RTT measurement.
 */
void stratum_rtt_start(stratum_module_t *module);

/**
 * Record a share response and update RTT statistics.
 * Call when pool responds to a submitted share.
 */
void stratum_rtt_record(stratum_module_t *module);

/**
 * Get the active pool configuration (primary or fallback).
 */
const pool_config_t *stratum_get_active_pool(const stratum_module_t *module);

#endif /* STRATUM_MODULE_H_ */
