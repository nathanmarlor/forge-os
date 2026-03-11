#include "config.h"
#include "nvs_config.h"
#include "event_bus.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "config_module";

// Mapping table: NVS key -> offset into config_module_t
typedef struct {
    const char *key;
    size_t offset;
} config_cache_entry_t;

#define CACHE_ENTRY(nvs_key, field) \
    { nvs_key, offsetof(config_module_t, field) }

static const config_cache_entry_t cache_map[] = {
    CACHE_ENTRY(NVS_CONFIG_ASIC_FREQ,                        asic_frequency),
    CACHE_ENTRY(NVS_CONFIG_ASIC_VOLTAGE,                     asic_voltage),
    CACHE_ENTRY(NVS_CONFIG_FAN_SPEED,                        fan_speed),
    CACHE_ENTRY(NVS_CONFIG_AUTO_FAN_SPEED,                   auto_fan),
    CACHE_ENTRY(NVS_CONFIG_FAN_TARGET_TEMP,                  fan_target_temp),
    CACHE_ENTRY(NVS_CONFIG_FAN_MIN_SPEED,                    fan_min_speed),
    CACHE_ENTRY(NVS_CONFIG_OVERHEAT_MODE,                    overheat_mode),
    CACHE_ENTRY(NVS_CONFIG_OVERCLOCK_ENABLED,                overclock_enabled),
    CACHE_ENTRY(NVS_CONFIG_USE_FALLBACK_STRATUM,             use_fallback_stratum),
    CACHE_ENTRY(NVS_CONFIG_STATS_FREQUENCY,                  stats_frequency),
    CACHE_ENTRY(NVS_CONFIG_STRATUM_DIFFICULTY,               stratum_difficulty),
    CACHE_ENTRY(NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE,     stratum_extranonce_subscribe),
    CACHE_ENTRY(NVS_CONFIG_STRATUM_DECODE_COINBASE,          stratum_decode_coinbase),
    CACHE_ENTRY(NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY,      fallback_stratum_difficulty),
    CACHE_ENTRY(NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUB,  fallback_stratum_extranonce_sub),
    CACHE_ENTRY(NVS_CONFIG_FALLBACK_STRATUM_DECODE_COINBASE, fallback_stratum_decode_coinbase),
    CACHE_ENTRY(NVS_CONFIG_STRATUM_PORT,                     stratum_port),
    CACHE_ENTRY(NVS_CONFIG_FALLBACK_STRATUM_PORT,            fallback_stratum_port),
};

#define CACHE_MAP_SIZE (sizeof(cache_map) / sizeof(cache_map[0]))

// Find the cached u16 pointer for a given NVS key, or NULL if not cached
static uint16_t *find_cache_field(config_module_t *module, const char *key)
{
    for (size_t i = 0; i < CACHE_MAP_SIZE; i++) {
        if (strcmp(cache_map[i].key, key) == 0) {
            return (uint16_t *)((uint8_t *)module + cache_map[i].offset);
        }
    }
    return NULL;
}

static const uint16_t *find_cache_field_const(const config_module_t *module, const char *key)
{
    for (size_t i = 0; i < CACHE_MAP_SIZE; i++) {
        if (strcmp(cache_map[i].key, key) == 0) {
            return (const uint16_t *)((const uint8_t *)module + cache_map[i].offset);
        }
    }
    return NULL;
}

// Default values for each cached key (used at init)
typedef struct {
    const char *key;
    uint16_t default_val;
} config_default_t;

static const config_default_t defaults[] = {
    { NVS_CONFIG_ASIC_FREQ,                        CONFIG_ASIC_FREQUENCY },
    { NVS_CONFIG_ASIC_VOLTAGE,                     CONFIG_ASIC_VOLTAGE },
    { NVS_CONFIG_FAN_SPEED,                        100 },
    { NVS_CONFIG_AUTO_FAN_SPEED,                   1 },
    { NVS_CONFIG_FAN_TARGET_TEMP,                  45 },
    { NVS_CONFIG_FAN_MIN_SPEED,                    35 },
    { NVS_CONFIG_OVERHEAT_MODE,                    0 },
    { NVS_CONFIG_OVERCLOCK_ENABLED,                0 },
    { NVS_CONFIG_USE_FALLBACK_STRATUM,             0 },
    { NVS_CONFIG_STATS_FREQUENCY,                  0 },
    { NVS_CONFIG_STRATUM_DIFFICULTY,               0 },
    { NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE,     0 },
    { NVS_CONFIG_STRATUM_DECODE_COINBASE,          0 },
    { NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY,      0 },
    { NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUB,  0 },
    { NVS_CONFIG_FALLBACK_STRATUM_DECODE_COINBASE, 0 },
    { NVS_CONFIG_STRATUM_PORT,                     3333 },
    { NVS_CONFIG_FALLBACK_STRATUM_PORT,            3333 },
};

#define DEFAULTS_SIZE (sizeof(defaults) / sizeof(defaults[0]))

static void publish_config_changed(const char *key, uint32_t new_value)
{
    event_t evt = event_create(EVT_CONFIG_CHANGED);
    strncpy(evt.data.config.key, key, sizeof(evt.data.config.key) - 1);
    evt.data.config.key[sizeof(evt.data.config.key) - 1] = '\0';
    evt.data.config.new_value = new_value;
    event_bus_publish(&evt);
}

esp_err_t config_init(config_module_t *module)
{
    if (module == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(module, 0, sizeof(*module));

    // Load all cached u16 values from NVS
    for (size_t i = 0; i < DEFAULTS_SIZE; i++) {
        uint16_t *field = find_cache_field(module, defaults[i].key);
        if (field != NULL) {
            *field = nvs_config_get_u16(defaults[i].key, defaults[i].default_val);
        }
    }

    module->initialized = true;
    ESP_LOGI(TAG, "Config module initialized (%zu cached keys)", CACHE_MAP_SIZE);
    return ESP_OK;
}

uint16_t config_get_u16(const config_module_t *module, const char *key, uint16_t default_val)
{
    if (module == NULL || !module->initialized) {
        return nvs_config_get_u16(key, default_val);
    }

    const uint16_t *cached = find_cache_field_const(module, key);
    if (cached != NULL) {
        return *cached;
    }

    // Key not in cache, fall through to NVS
    return nvs_config_get_u16(key, default_val);
}

esp_err_t config_set_u16(config_module_t *module, const char *key, uint16_t value)
{
    if (module == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check if value actually changed (from cache or NVS)
    uint16_t old_value;
    uint16_t *cached = find_cache_field(module, key);
    if (cached != NULL) {
        old_value = *cached;
    } else {
        old_value = nvs_config_get_u16(key, value);
    }

    // Always write to NVS
    nvs_config_set_u16(key, value);

    // Update cache
    if (cached != NULL) {
        *cached = value;
    }

    // Publish change event only if value changed
    if (old_value != value) {
        ESP_LOGD(TAG, "Config changed: %s = %u (was %u)", key, value, old_value);
        publish_config_changed(key, (uint32_t)value);
    }

    return ESP_OK;
}

char *config_get_string(const config_module_t *module, const char *key, const char *default_val)
{
    (void)module; // No string caching yet
    return nvs_config_get_string(key, default_val);
}

esp_err_t config_set_string(config_module_t *module, const char *key, const char *value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    (void)module; // No string caching yet
    nvs_config_set_string(key, value);
    publish_config_changed(key, 0);

    return ESP_OK;
}
