#include "power_module.h"
#include <string.h>

void power_module_init(power_module_t *module)
{
    memset(module, 0, sizeof(*module));
    module->frequency_multiplier = 1.0f;
    module->initialized = true;
}
