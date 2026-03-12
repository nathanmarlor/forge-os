#include "esp_log.h"
#include "esp_timer.h"
#include "connect.h"
#include "system.h"
#include "lwip/dns.h"
#include <lwip/tcpip.h>
#include "nvs_config.h"
#include "stratum_task.h"
#include "work_queue.h"
#include "esp_wifi.h"
#include <esp_sntp.h>
#include <time.h>
#include "coinbase_decoder.h"
#include "app_context.h"
#include "stats.h"
#include "stratum_module.h"
#include "asic_module.h"

#define STRATUM_PW CONFIG_STRATUM_PW
#define FALLBACK_STRATUM_PW CONFIG_FALLBACK_STRATUM_PW
#define STRATUM_DIFFICULTY CONFIG_STRATUM_DIFFICULTY

#define MAX_RETRY_ATTEMPTS 3
#define MAX_CRITICAL_RETRY_ATTEMPTS 5

#define BUFFER_SIZE 1024

static const char * TAG = "stratum_task";

static StratumApiV1Message stratum_api_v1_message = {};

static const char * primary_stratum_url;
static uint16_t primary_stratum_port;

struct timeval tcp_snd_timeout = {
    .tv_sec = 5,
    .tv_usec = 0
};

struct timeval tcp_rcv_timeout = {
    .tv_sec = 60 * 3,
    .tv_usec = 0
};

bool is_wifi_connected() {
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}

static void clean_queue(app_context_t *ctx)
{
    ctx->abandon_work = 1;
    queue_clear(&ctx->stratum_queue);

    pthread_mutex_lock(ctx->asic.jobs_lock);
    ASIC_jobs_queue_clear(&ctx->ASIC_jobs_queue);
    memset(ctx->asic.valid_jobs, 0, sizeof(ctx->asic.valid_jobs));
    pthread_mutex_unlock(ctx->asic.jobs_lock);
}

void stratum_close_connection(void)
{
    extern app_context_t APP_CONTEXT;
    stratum_module_t *strat = &APP_CONTEXT.stratum;

    pthread_mutex_lock(&strat->connection_lock);
    if (strat->sock < 0) {
        pthread_mutex_unlock(&strat->connection_lock);
        ESP_LOGE(TAG, "Socket already shutdown, not shutting down again..");
        return;
    }

    ESP_LOGE(TAG, "Shutting down socket and restarting...");
    shutdown(strat->sock, SHUT_RDWR);
    close(strat->sock);
    strat->sock = -1;
    pthread_mutex_unlock(&strat->connection_lock);
    clean_queue(&APP_CONTEXT);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

static void sync_clock(uint32_t ntime)
{
    static uint32_t last_clock_sync = 0;
    if (last_clock_sync + (60 * 60) > ntime) {
        return;
    }
    ESP_LOGI(TAG, "Syncing clock");
    last_clock_sync = ntime;
    struct timeval tv;
    tv.tv_sec = ntime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
}

void stratum_primary_heartbeat(void * pvParameters)
{
    (void)pvParameters;
    extern app_context_t APP_CONTEXT;
    stratum_module_t *strat = &APP_CONTEXT.stratum;

    ESP_LOGI(TAG, "Starting heartbeat thread for primary pool: %s:%d", primary_stratum_url, primary_stratum_port);
    vTaskDelay(10000 / portTICK_PERIOD_MS);

    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;

    struct timeval tcp_timeout = {
        .tv_sec = 5,
        .tv_usec = 0
    };

    while (1)
    {
        if (!strat->is_using_fallback) {
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        char host_ip[INET_ADDRSTRLEN];
        ESP_LOGD(TAG, "Running Heartbeat on: %s!", primary_stratum_url);

        if (!is_wifi_connected()) {
            ESP_LOGD(TAG, "Heartbeat. Failed WiFi check!");
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        struct hostent *primary_dns_addr = gethostbyname(primary_stratum_url);
        if (primary_dns_addr == NULL) {
            ESP_LOGD(TAG, "Heartbeat. Failed DNS check for: %s!", primary_stratum_url);
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }
        inet_ntop(AF_INET, (void *)primary_dns_addr->h_addr_list[0], host_ip, sizeof(host_ip));

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(primary_stratum_port);

        int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGD(TAG, "Heartbeat. Failed socket create check!");
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in));
        if (err != 0)
        {
            ESP_LOGD(TAG, "Heartbeat. Failed connect check: %s:%d (errno %d: %s)", host_ip, primary_stratum_port, errno, strerror(errno));
            close(sock);
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO , &tcp_timeout, sizeof(tcp_timeout)) != 0) {
            ESP_LOGE(TAG, "Fail to setsockopt SO_RCVTIMEO ");
        }

        int send_uid = 1;
        STRATUM_V1_subscribe(sock, send_uid++, APP_CONTEXT.asic_model_str);
        STRATUM_V1_authenticate(sock, send_uid++, strat->primary.username, strat->primary.password);

        char recv_buffer[BUFFER_SIZE];
        memset(recv_buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sock, recv_buffer, BUFFER_SIZE - 1, 0);

        shutdown(sock, SHUT_RDWR);
        close(sock);

        if (bytes_received == -1)  {
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        if (strstr(recv_buffer, "mining.notify") != NULL) {
            ESP_LOGI(TAG, "Heartbeat successful and in fallback mode. Switching back to primary.");
            strat->is_using_fallback = false;
            // Close connection to trigger reconnect to primary
            stratum_close_connection();
            continue;
        }

        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}

static void decode_mining_notification(stratum_module_t *strat,
                                       const mining_notify * notification)
{
    extern app_context_t APP_CONTEXT;
    if (!strat->extranonce_str) return;

    mining_notification_result_t result;
    memset(&result, 0, sizeof(result));

    const pool_config_t *pool = stratum_get_active_pool(strat);

    if (coinbase_process_notification(notification,
                                      strat->extranonce_str,
                                      strat->extranonce_2_len,
                                      pool->username,
                                      pool->decode_coinbase,
                                      &result) != ESP_OK) {
        return;
    }

    strat->network_nonce_diff = (uint64_t) result.network_difficulty;

    if ((int)result.block_height != strat->block_height) {
        ESP_LOGI(TAG, "Block height %d", result.block_height);
        strat->block_height = result.block_height;
    }

    if (result.scriptsig) {
        if (strcmp(result.scriptsig, strat->scriptsig) != 0) {
            ESP_LOGI(TAG, "Scriptsig: %s", result.scriptsig);
            strncpy(strat->scriptsig, result.scriptsig, sizeof(strat->scriptsig) - 1);
            strat->scriptsig[sizeof(strat->scriptsig) - 1] = '\0';
        }
        free(result.scriptsig);
    }

    // Store coinbase data for HTTP API
    if (result.decoding_enabled && result.output_count > 0) {
        APP_CONTEXT.coinbase_output_count = result.output_count;
        memcpy(APP_CONTEXT.coinbase_outputs, result.outputs,
               sizeof(coinbase_output_t) * result.output_count);
        APP_CONTEXT.coinbase_value_total_satoshis = result.total_value_satoshis;
    }
}

void stratum_task(void * pvParameters)
{
    extern app_context_t APP_CONTEXT;
    stratum_module_t *strat = &APP_CONTEXT.stratum;

    primary_stratum_url = strat->primary.url;
    primary_stratum_port = strat->primary.port;
    char * stratum_url = strat->primary.url;
    uint16_t port = strat->primary.port;

    strat->sock = -1;
    strat->send_uid = 1;

    STRATUM_V1_initialize_buffer();
    char host_ip[20];
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    int retry_attempts = 0;
    int retry_critical_attempts = 0;

    xTaskCreate(stratum_primary_heartbeat, "stratum primary heartbeat", 8192, pvParameters, 1, NULL);

    ESP_LOGI(TAG, "Opening connection to pool: %s:%d", stratum_url, port);
    while (1) {
        if (!is_wifi_connected()) {
            ESP_LOGI(TAG, "WiFi disconnected, attempting to reconnect...");
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        if (retry_attempts >= MAX_RETRY_ATTEMPTS)
        {
            if (strat->fallback.url == NULL || strat->fallback.url[0] == '\0') {
                ESP_LOGI(TAG, "Unable to switch to fallback. No url configured. (retries: %d)...", retry_attempts);
                strat->is_using_fallback = false;
                retry_attempts = 0;
                continue;
            }

            strat->is_using_fallback = !strat->is_using_fallback;
            ESP_LOGI(TAG, "Switching target due to too many failures (retries: %d)...", retry_attempts);
            retry_attempts = 0;
        }

        const pool_config_t *pool = stratum_get_active_pool(strat);
        stratum_url = pool->url;
        port = pool->port;

        struct hostent *dns_addr = gethostbyname(stratum_url);
        if (dns_addr == NULL) {
            retry_attempts++;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        inet_ntop(AF_INET, (void *)dns_addr->h_addr_list[0], host_ip, sizeof(host_ip));

        ESP_LOGI(TAG, "Connecting to: stratum+tcp://%s:%d (%s)", stratum_url, port, host_ip);

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);

        strat->sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (strat->sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            if (++retry_critical_attempts > MAX_CRITICAL_RETRY_ATTEMPTS) {
                ESP_LOGE(TAG, "Max retry attempts reached, restarting...");
                esp_restart();
            }
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }
        retry_critical_attempts = 0;

        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, port);
        int err = connect(strat->sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in));
        if (err != 0)
        {
            retry_attempts++;
            ESP_LOGE(TAG, "Socket unable to connect to %s:%d (errno %d: %s)", stratum_url, port, errno, strerror(errno));
            shutdown(strat->sock, SHUT_RDWR);
            close(strat->sock);
            strat->sock = -1;
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        if (setsockopt(strat->sock, SOL_SOCKET, SO_SNDTIMEO, &tcp_snd_timeout, sizeof(tcp_snd_timeout)) != 0) {
            ESP_LOGE(TAG, "Fail to setsockopt SO_SNDTIMEO");
        }

        if (setsockopt(strat->sock, SOL_SOCKET, SO_RCVTIMEO , &tcp_rcv_timeout, sizeof(tcp_rcv_timeout)) != 0) {
            ESP_LOGE(TAG, "Fail to setsockopt SO_RCVTIMEO ");
        }

        strat->send_uid = 1;
        clean_queue(&APP_CONTEXT);

        ///// Start Stratum Action
        // mining.configure - ID: 1
        STRATUM_V1_configure_version_rolling(strat->sock, strat->send_uid++, &strat->version_mask);

        // mining.subscribe - ID: 2
        STRATUM_V1_subscribe(strat->sock, strat->send_uid++, APP_CONTEXT.asic_model_str);

        //mining.authorize - ID: 3
        STRATUM_V1_authenticate(strat->sock, strat->send_uid++, pool->username, pool->password);

        //mining.suggest_difficulty - ID: 4
        uint16_t suggested_difficulty = pool->suggested_difficulty;
        if (suggested_difficulty == 0) {
            suggested_difficulty = STRATUM_DIFFICULTY;
        }
        STRATUM_V1_suggest_difficulty(strat->sock, strat->send_uid++, suggested_difficulty);

        //mining.extranonce.subscribe
        if (pool->extranonce_subscribe) {
            STRATUM_V1_extranonce_subscribe(strat->sock, strat->send_uid++);
        }

        // Everything is set up, lets make sure we don't abandon work unnecessarily.
        APP_CONTEXT.abandon_work = 0;

        // Force the first mining.notify to be treated as clean_jobs.
        // After a reconnect, the ASIC may still have old work loaded.
        // Without this, the pool could send a non-clean notify and the
        // old ASIC jobs would coexist with new ones, causing stale submissions.
        bool force_clean = true;

        while (1) {
            taskYIELD(); // allow IDLE task to reset watchdog when recv() is not blocking
            char * line = STRATUM_V1_receive_jsonrpc_line(strat->sock);
            if (!line) {
                ESP_LOGE(TAG, "Failed to receive JSON-RPC line, reconnecting...");
                retry_attempts++;
                stratum_close_connection();
                break;
            }

            ESP_LOGD(TAG, "rx: %s", line); // debug incoming stratum messages
            STRATUM_V1_parse(&stratum_api_v1_message, line);
            free(line);

            if (stratum_api_v1_message.method == MINING_NOTIFY) {
                sync_clock(stratum_api_v1_message.mining_notification->ntime);
                decode_mining_notification(strat, stratum_api_v1_message.mining_notification);
                bool should_clean = stratum_api_v1_message.should_abandon_work || force_clean;
                if (should_clean &&
                    (APP_CONTEXT.stratum_queue.count > 0 || APP_CONTEXT.ASIC_jobs_queue.count > 0)) {
                    ESP_LOGI(TAG, "Clean Jobs: new_job=%s (abandoning %d queued + %d ASIC jobs)%s",
                             stratum_api_v1_message.mining_notification->job_id,
                             APP_CONTEXT.stratum_queue.count,
                             APP_CONTEXT.ASIC_jobs_queue.count,
                             force_clean ? " [reconnect]" : "");
                    clean_queue(&APP_CONTEXT);
                }
                force_clean = false;
                // Drop oldest notification if queue is full (non-blocking to avoid TOCTOU deadlock)
                mining_notify *dropped = (mining_notify *)queue_try_dequeue(&APP_CONTEXT.stratum_queue);
                if (dropped) {
                    STRATUM_V1_free_mining_notify(dropped);
                }
                stratum_api_v1_message.mining_notification->difficulty = strat->stratum_difficulty;
                queue_enqueue(&APP_CONTEXT.stratum_queue, stratum_api_v1_message.mining_notification);
            } else if (stratum_api_v1_message.method == MINING_SET_DIFFICULTY) {
                uint32_t new_diff = stratum_api_v1_message.new_difficulty;
                if (new_diff > 0) {
                    if (new_diff != strat->stratum_difficulty) {
                        strat->stratum_difficulty = new_diff;
                        ESP_LOGI(TAG, "Set stratum difficulty: %lu", strat->stratum_difficulty);
                    }
                }
            } else if (stratum_api_v1_message.method == MINING_SET_VERSION_MASK ||
                    stratum_api_v1_message.method == STRATUM_RESULT_VERSION_MASK) {
                ESP_LOGI(TAG, "Set version mask: %08lx", stratum_api_v1_message.version_mask);
                strat->version_mask = stratum_api_v1_message.version_mask;
                strat->new_version_rolling_msg = true;
            } else if (stratum_api_v1_message.method == STRATUM_RESULT_SUBSCRIBE) {
                pthread_mutex_lock(&strat->connection_lock);
                char *old_extranonce = strat->extranonce_str;
                strat->extranonce_str = stratum_api_v1_message.extranonce_str;
                strat->extranonce_2_len = stratum_api_v1_message.extranonce_2_len;
                pthread_mutex_unlock(&strat->connection_lock);
                free(old_extranonce);
            } else if (stratum_api_v1_message.method == CLIENT_RECONNECT) {
                ESP_LOGE(TAG, "Pool requested client reconnect...");
                stratum_close_connection();
                break;
            } else if (stratum_api_v1_message.method == STRATUM_RESULT) {
                stratum_rtt_record(strat);

                if (stratum_api_v1_message.response_success) {
                    ESP_LOGI(TAG, "message result accepted");
                    stats_notify_accepted_share(&APP_CONTEXT.stats);
                    SYSTEM_led_blink(1);
                } else {
                    ESP_LOGW(TAG, "message result rejected: %s", stratum_api_v1_message.error_str);
                    stats_notify_rejected_share(&APP_CONTEXT.stats, stratum_api_v1_message.error_str);
                }
            } else if (stratum_api_v1_message.method == STRATUM_RESULT_SETUP) {
                // Reset retry attempts after successfully receiving data.
                retry_attempts = 0;
                if (stratum_api_v1_message.response_success) {
                    ESP_LOGI(TAG, "setup message accepted");
                } else {
                    ESP_LOGE(TAG, "setup message rejected: %s", stratum_api_v1_message.error_str);
                }
            }
        }
    }
    vTaskDelete(NULL);
}
