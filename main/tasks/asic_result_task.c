#include <lwip/tcpip.h>

#include "system.h"
#include "work_queue.h"
#include "serial.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_config.h"
#include "utils.h"
#include "stratum_task.h"
#include "asic.h"
#include "hashrate_monitor_task.h"
#include "app_context.h"
#include "stats.h"
#include "stratum_module.h"
#include "asic_module.h"

static const char *TAG = "asic_result";

void ASIC_result_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    extern app_context_t APP_CONTEXT;
    asic_module_t *asic = &APP_CONTEXT.asic;

    while (1)
    {
        task_result *asic_result = ASIC_process_work(GLOBAL_STATE);

        if (asic_result == NULL)
        {
            continue;
        }

        // Check if this is a register response
        if (asic_result->register_type != REGISTER_INVALID)
        {
            ESP_LOGD(TAG, "Register response detected: type=%d, asic=%d, value=0x%08X",
                     asic_result->register_type, asic_result->asic_nr, asic_result->value);
            if (APP_CONTEXT.stats.hashrate_initialized) {
                stats_handle_register_read(&APP_CONTEXT.stats, asic_result->register_type, asic_result->asic_nr, asic_result->value);
            }
            continue;
        }

        uint8_t job_id = asic_result->job_id;

        pthread_mutex_lock(asic->jobs_lock);

        if (asic->valid_jobs[job_id] == 0)
        {
            pthread_mutex_unlock(asic->jobs_lock);
            ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
            continue;
        }

        // check the nonce difficulty
        double nonce_diff = test_nonce_value(
            asic->active_jobs[job_id],
            asic_result->nonce,
            asic_result->rolled_version);

        ESP_LOGD(TAG, "Ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %ld.",
                 asic_result->rolled_version, asic_result->nonce, nonce_diff,
                 asic->active_jobs[job_id]->pool_diff);

        bool should_submit = (nonce_diff >= asic->active_jobs[job_id]->pool_diff);

        // Copy job data needed for submission before releasing the lock.
        // The socket write can block, so we must not hold the lock during it.
        char jobid_buf[64] = {0};
        char extranonce2_buf[64] = {0};
        uint32_t ntime = 0;
        uint32_t job_version = 0;
        if (should_submit) {
            strncpy(jobid_buf, asic->active_jobs[job_id]->jobid, sizeof(jobid_buf) - 1);
            strncpy(extranonce2_buf, asic->active_jobs[job_id]->extranonce2, sizeof(extranonce2_buf) - 1);
            ntime = asic->active_jobs[job_id]->ntime;
            job_version = asic->active_jobs[job_id]->version;
        }

        pthread_mutex_unlock(asic->jobs_lock);

        if (should_submit)
        {
            char *user = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback
                ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_user
                : GLOBAL_STATE->SYSTEM_MODULE.pool_user;
            stratum_rtt_start(&APP_CONTEXT.stratum);
            int ret = STRATUM_V1_submit_share(
                GLOBAL_STATE->sock,
                GLOBAL_STATE->send_uid++,
                user,
                jobid_buf,
                extranonce2_buf,
                ntime,
                asic_result->nonce,
                asic_result->rolled_version ^ job_version);

            if (ret < 0) {
                ESP_LOGI(TAG, "Unable to write share to socket. Closing connection. Ret: %d (errno %d: %s)", ret, errno, strerror(errno));
                stratum_close_connection(GLOBAL_STATE);
            }
        }

        // Track best difficulty via stats module
        {
            double network_diff = (double)GLOBAL_STATE->network_nonce_diff;
            stats_check_best_diff(&APP_CONTEXT.stats, nonce_diff, network_diff);
        }
    }
}
