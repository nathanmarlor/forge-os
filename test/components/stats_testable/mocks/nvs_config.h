#ifndef MOCK_NVS_CONFIG_H
#define MOCK_NVS_CONFIG_H

/**
 * Mock NVS config for unit testing.
 * Provides the same key #defines and function signatures as the real
 * nvs_config.h, but backed by simple in-memory storage.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Key defines (must match real nvs_config.h)
#define NVS_CONFIG_BEST_DIFF "bestdiff"

// All other keys referenced by stats or test code
#define NVS_CONFIG_WIFI_SSID "wifissid"
#define NVS_CONFIG_WIFI_PASS "wifipass"
#define NVS_CONFIG_HOSTNAME "hostname"
#define NVS_CONFIG_STRATUM_URL "stratumurl"
#define NVS_CONFIG_STRATUM_PORT "stratumport"
#define NVS_CONFIG_FALLBACK_STRATUM_URL "fbstratumurl"
#define NVS_CONFIG_FALLBACK_STRATUM_PORT "fbstratumport"
#define NVS_CONFIG_STRATUM_USER "stratumuser"
#define NVS_CONFIG_STRATUM_PASS "stratumpass"
#define NVS_CONFIG_FALLBACK_STRATUM_USER "fbstratumuser"
#define NVS_CONFIG_FALLBACK_STRATUM_PASS "fbstratumpass"
#define NVS_CONFIG_ASIC_FREQ "asicfrequency"
#define NVS_CONFIG_ASIC_VOLTAGE "asicvoltage"
#define NVS_CONFIG_ASIC_MODEL "asicmodel"
#define NVS_CONFIG_DEVICE_MODEL "devicemodel"
#define NVS_CONFIG_BOARD_VERSION "boardversion"
#define NVS_CONFIG_AUTO_FAN_SPEED "autofanspeed"
#define NVS_CONFIG_FAN_SPEED "fanspeed"
#define NVS_CONFIG_PRODUCTION_TEST "productiontest"
#define NVS_CONFIG_OVERHEAT_MODE "overheat_mode"
#define NVS_CONFIG_OVERCLOCK_ENABLED "oc_enabled"
#define NVS_CONFIG_SWARM "swarmconfig"
#define NVS_CONFIG_FAN_TARGET_TEMP "fantargettemp"
#define NVS_CONFIG_FAN_MIN_SPEED "fanminspeed"
#define NVS_CONFIG_USE_FALLBACK_STRATUM "usefallback"
#define NVS_CONFIG_STATS_FREQUENCY "statsfreq"
#define NVS_CONFIG_STRATUM_DIFFICULTY "stratumdiff"
#define NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE "stratumxnsub"
#define NVS_CONFIG_STRATUM_DECODE_COINBASE "stratumdecode"
#define NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY "fbstratumdiff"
#define NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUB "fbstratumxnsub"
#define NVS_CONFIG_FALLBACK_STRATUM_DECODE_COINBASE "fbstratumdecode"

// Mock storage — single u64 slot is enough for stats tests
// Declared extern so all translation units share the same state
extern uint64_t _mock_nvs_u64_value;
extern bool _mock_nvs_u64_set;

uint64_t nvs_config_get_u64(const char *key, uint64_t default_val);
void nvs_config_set_u64(const char *key, uint64_t value);
void mock_nvs_reset(void);

#endif /* MOCK_NVS_CONFIG_H */
