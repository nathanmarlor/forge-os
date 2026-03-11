#ifndef MAIN_NVS_DEVICE_H
#define MAIN_NVS_DEVICE_H

#include "esp_err.h"
#include "app_context.h"

esp_err_t NVSDevice_init(void);
esp_err_t NVSDevice_parse_config(app_context_t *ctx);

#endif // MAIN_NVS_DEVICE_H
