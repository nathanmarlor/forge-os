#ifndef MAIN_H_
#define MAIN_H_

#include "connect.h"

void MINER_set_wifi_status(wifi_status_t status, int retry_count, int reason);
void MINER_set_ap_status(bool enabled);

#endif /* MAIN_H_ */