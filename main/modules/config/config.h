#ifndef CONFIG_MODULE_H
#define CONFIG_MODULE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "esp_err.h"

/**
 * Config Module
 *
 * Wraps NVS access with an in-memory cache and publishes EVT_CONFIG_CHANGED
 * events when values are modified. Subscribers (power, stratum) react to
 * changes instead of polling NVS every 2 seconds.
 */

typedef struct {
    // Hardware
    uint16_t asic_frequency;
    uint16_t asic_voltage;
    // Fan
    uint16_t fan_speed;
    uint16_t auto_fan;
    uint16_t fan_target_temp;
    uint16_t fan_min_speed;
    // Misc
    uint16_t overheat_mode;
    uint16_t overclock_enabled;
    // Pool management
    uint16_t use_fallback_stratum;
    // Statistics
    uint16_t stats_frequency;
    // Stratum advanced (primary)
    uint16_t stratum_difficulty;
    uint16_t stratum_extranonce_subscribe;
    uint16_t stratum_decode_coinbase;
    // Stratum advanced (fallback)
    uint16_t fallback_stratum_difficulty;
    uint16_t fallback_stratum_extranonce_sub;
    uint16_t fallback_stratum_decode_coinbase;
    // Stratum ports
    uint16_t stratum_port;
    uint16_t fallback_stratum_port;

    bool initialized;
    pthread_mutex_t lock;
} config_module_t;

/**
 * Initialize the config module: load all u16 keys from NVS into the cache.
 */
esp_err_t config_init(config_module_t *module);

/**
 * Get a cached u16 config value. Falls back to NVS if key is not in the
 * fast-path cache (for keys added later without a cache field).
 */
uint16_t config_get_u16(const config_module_t *module, const char *key, uint16_t default_val);

/**
 * Set a u16 config value. Writes to NVS, updates the cache, and publishes
 * EVT_CONFIG_CHANGED if the value actually changed.
 */
esp_err_t config_set_u16(config_module_t *module, const char *key, uint16_t value);

/**
 * String config pass-through (no caching, but publishes EVT_CONFIG_CHANGED).
 * Caller must free the returned string from config_get_string.
 */
char *config_get_string(const config_module_t *module, const char *key, const char *default_val);
esp_err_t config_set_string(config_module_t *module, const char *key, const char *value);

#endif /* CONFIG_MODULE_H */
