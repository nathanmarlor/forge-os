#ifndef SYSTEM_H_
#define SYSTEM_H_

#include "esp_err.h"
#include "global_state.h"

void SYSTEM_init_system(GlobalState * GLOBAL_STATE);
esp_err_t SYSTEM_init_peripherals(GlobalState * GLOBAL_STATE);

// LED control (1 = share LED, 2 = status LED)
void SYSTEM_led_blink(int num);

#endif /* SYSTEM_H_ */
