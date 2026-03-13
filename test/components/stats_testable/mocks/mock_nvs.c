#include "nvs_config.h"

uint64_t _mock_nvs_u64_value = 0;
bool _mock_nvs_u64_set = false;

uint64_t nvs_config_get_u64(const char *key, uint64_t default_val)
{
    (void)key;
    return _mock_nvs_u64_set ? _mock_nvs_u64_value : default_val;
}

void nvs_config_set_u64(const char *key, uint64_t value)
{
    (void)key;
    _mock_nvs_u64_value = value;
    _mock_nvs_u64_set = true;
}

void mock_nvs_reset(void)
{
    _mock_nvs_u64_value = 0;
    _mock_nvs_u64_set = false;
}
