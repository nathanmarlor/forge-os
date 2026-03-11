#include <sys/time.h>
#include <limits.h>

#include "work_queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mining.h"
#include "string.h"

#include "asic.h"
#include "app_context.h"
#include "asic_module.h"
#include "stratum_module.h"

static const char *TAG = "create_jobs_task";

#define QUEUE_LOW_WATER_MARK 10

void create_jobs_task(void *pvParameters)
{
    extern app_context_t APP_CONTEXT;
    stratum_module_t *strat = &APP_CONTEXT.stratum;

    while (1)
    {
        mining_notify *mining_notification = (mining_notify *)queue_dequeue(&APP_CONTEXT.stratum_queue);
        if (mining_notification == NULL) {
            ESP_LOGE(TAG, "Failed to dequeue mining notification");
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "New Work Dequeued %s", mining_notification->job_id);

        if (strat->new_version_rolling_msg) {
            ESP_LOGI(TAG, "Set chip version rolls %i", (int)(strat->version_mask >> 13));
            ASIC_set_version_mask(APP_CONTEXT.device_model, strat->version_mask);
            strat->new_version_rolling_msg = false;
        }

        uint32_t extranonce_2 = 0;
        while (APP_CONTEXT.stratum_queue.count < 1 && APP_CONTEXT.abandon_work == 0)
        {
            if (APP_CONTEXT.ASIC_jobs_queue.count < QUEUE_LOW_WATER_MARK)
            {
                char *extranonce_2_str = extranonce_2_generate(extranonce_2, strat->extranonce_2_len);
                if (extranonce_2_str == NULL) {
                    ESP_LOGE(TAG, "Failed to generate extranonce_2");
                    break;
                }

                char *coinbase_tx = construct_coinbase_tx(
                    mining_notification->coinbase_1, mining_notification->coinbase_2,
                    strat->extranonce_str, extranonce_2_str);
                if (coinbase_tx == NULL) {
                    ESP_LOGE(TAG, "Failed to construct coinbase_tx");
                    free(extranonce_2_str);
                    break;
                }

                char *merkle_root = calculate_merkle_root_hash(
                    coinbase_tx, (uint8_t(*)[32])mining_notification->merkle_branches,
                    mining_notification->n_merkle_branches);
                if (merkle_root == NULL) {
                    ESP_LOGE(TAG, "Failed to calculate merkle_root");
                    free(extranonce_2_str);
                    free(coinbase_tx);
                    break;
                }

                bm_job next_job = construct_bm_job(mining_notification, merkle_root, strat->version_mask);

                bm_job *queued_next_job = malloc(sizeof(bm_job));
                if (queued_next_job == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for queued_next_job");
                    free(extranonce_2_str);
                    free(coinbase_tx);
                    free(merkle_root);
                    break;
                }

                memcpy(queued_next_job, &next_job, sizeof(bm_job));
                queued_next_job->extranonce2 = extranonce_2_str;
                queued_next_job->jobid = strdup(mining_notification->job_id);
                queued_next_job->version_mask = strat->version_mask;

                queue_enqueue(&APP_CONTEXT.ASIC_jobs_queue, queued_next_job);

                free(coinbase_tx);
                free(merkle_root);

                extranonce_2++;
            }
            else
            {
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
        }

        if (APP_CONTEXT.abandon_work == 1)
        {
            APP_CONTEXT.abandon_work = 0;
            ASIC_jobs_queue_clear(&APP_CONTEXT.ASIC_jobs_queue);
            xSemaphoreGive(APP_CONTEXT.asic.dispatch_semaphore);
        }

        STRATUM_V1_free_mining_notify(mining_notification);
    }
}
