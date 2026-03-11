#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "app_context.h"
#include "screen.h"

// static const char * TAG = "screen";

extern const lv_img_dsc_t logo;

static lv_obj_t * screens[MAX_SCREENS];

static screen_t current_screen = -1;
static TickType_t current_screen_counter;

static lv_obj_t *asic_status_label;

static lv_obj_t *hashrate_label;
static lv_obj_t *efficiency_label;
static lv_obj_t *difficulty_label;
static lv_obj_t *chip_temp_label;

static lv_obj_t *firmware_update_scr_filename_label;
static lv_obj_t *firmware_update_scr_status_label;
static lv_obj_t *ip_addr_scr_overheat_label;
static lv_obj_t *ip_addr_scr_urls_label;
static lv_obj_t *mining_url_scr_urls_label;
static lv_obj_t *wifi_status_label;

static lv_obj_t *self_test_message_label;
static lv_obj_t *self_test_result_label;
static lv_obj_t *self_test_finished_label_pass;
static lv_obj_t *self_test_finished_label_fail;

static double current_hashrate;
static float current_power;
static uint64_t current_difficulty;
static float current_chip_temp;
static bool found_block;

#define SCREEN_UPDATE_MS 500
#define LOGO_DELAY_COUNT 5000 / SCREEN_UPDATE_MS
#define CAROUSEL_DELAY_COUNT 10000 / SCREEN_UPDATE_MS

static lv_obj_t * create_scr_self_test() {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "NANO SELF TEST");

    self_test_message_label = lv_label_create(scr);
    self_test_result_label = lv_label_create(scr);

    self_test_finished_label_pass = lv_label_create(scr);
    lv_obj_set_width(self_test_finished_label_pass, LV_HOR_RES);
    lv_obj_add_flag(self_test_finished_label_pass, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(self_test_finished_label_pass, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(self_test_finished_label_pass, "Press RESET button to start Nano.");

    self_test_finished_label_fail = lv_label_create(scr);
    lv_obj_set_width(self_test_finished_label_fail, LV_HOR_RES);
    lv_obj_add_flag(self_test_finished_label_fail, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(self_test_finished_label_fail, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(self_test_finished_label_fail, "Hold BOOT button for 2 seconds to cancel self test, or press RESET to run again.");

    return scr;
}

static lv_obj_t * create_scr_overheat(wifi_state_t *wifi) {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "DEVICE OVERHEAT!");

    lv_obj_t *label2 = lv_label_create(scr);
    lv_obj_set_width(label2, LV_HOR_RES);
    lv_label_set_long_mode(label2, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label2, "Power, frequency and fan configurations have been reset. Go to AxeOS to reconfigure device.");

    lv_obj_t *label3 = lv_label_create(scr);
    lv_label_set_text(label3, "Device IP:");

    ip_addr_scr_overheat_label = lv_label_create(scr);
    lv_label_set_text(ip_addr_scr_overheat_label, wifi->ip_addr_str);

    return scr;
}

static lv_obj_t * create_scr_asic_status() {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "ASIC STATUS:");

    asic_status_label = lv_label_create(scr);
    lv_label_set_long_mode(asic_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    return scr;
}

static lv_obj_t * create_scr_configure(wifi_state_t *wifi) {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_obj_set_width(label1, LV_HOR_RES);
    lv_obj_set_style_anim_duration(label1, 15000, LV_PART_MAIN);
    lv_label_set_long_mode(label1, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label1, "Welcome to your new Nano! Connect to the configuration Wi-Fi and connect the Nano to your network.");

    // skip a line, it looks nicer this way
    lv_label_create(scr);

    lv_obj_t *label2 = lv_label_create(scr);
    lv_label_set_text(label2, "Wi-Fi (for setup):");

    lv_obj_t *label3 = lv_label_create(scr);
    lv_label_set_text(label3, wifi->ap_ssid);

    return scr;
}

static lv_obj_t * create_scr_ota() {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_obj_set_width(label1, LV_HOR_RES);
    lv_label_set_text(label1, "Firmware update");

    firmware_update_scr_filename_label = lv_label_create(scr);

    firmware_update_scr_status_label = lv_label_create(scr);

    return scr;
}

static lv_obj_t * create_scr_connection(wifi_state_t *wifi) {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_obj_set_width(label1, LV_HOR_RES);
    lv_label_set_long_mode(label1, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text_fmt(label1, "Wi-Fi: %s", wifi->ssid);

    wifi_status_label = lv_label_create(scr);
    lv_label_set_text(wifi_status_label, wifi->wifi_status);

    lv_obj_t *label3 = lv_label_create(scr);
    lv_label_set_text(label3, "Wi-Fi (for setup):");

    lv_obj_t *label4 = lv_label_create(scr);
    lv_label_set_text(label4, wifi->ap_ssid);

    return scr;
}

static lv_obj_t * create_scr_logo() {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, &logo);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    return scr;
}

static lv_obj_t * create_scr_urls(stratum_module_t *strat, wifi_state_t *wifi) {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "Stratum Host:");

    mining_url_scr_urls_label = lv_label_create(scr);
    lv_obj_set_width(mining_url_scr_urls_label, LV_HOR_RES);
    lv_label_set_long_mode(mining_url_scr_urls_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(mining_url_scr_urls_label, strat->is_using_fallback ? strat->fallback.url : strat->primary.url);

    lv_obj_t *label3 = lv_label_create(scr);
    lv_label_set_text(label3, "Nano IP:");

    ip_addr_scr_urls_label = lv_label_create(scr);
    lv_label_set_text(ip_addr_scr_urls_label, wifi->ip_addr_str);

    return scr;
}

static lv_obj_t * create_scr_stats() {
    lv_obj_t * scr = lv_obj_create(NULL);

    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    hashrate_label = lv_label_create(scr);
    lv_label_set_text(hashrate_label, "Gh/s: --");

    efficiency_label = lv_label_create(scr);
    lv_label_set_text(efficiency_label, "J/Th: --");

    difficulty_label = lv_label_create(scr);
    lv_label_set_text(difficulty_label, "Best: --");

    chip_temp_label = lv_label_create(scr);
    lv_label_set_text(chip_temp_label, "Temp: --");

    return scr;
}

static void screen_show(screen_t screen)
{
    if (current_screen != screen) {
        lv_obj_t * scr = screens[screen];

        if (scr && lvgl_port_lock(0)) {
            lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, LV_DEF_REFR_PERIOD * 128 / 8, 0, false);
            lvgl_port_unlock();
        }

        current_screen = screen;
        current_screen_counter = 0;
    }
}

static void screen_update_cb(lv_timer_t * timer)
{
    extern app_context_t APP_CONTEXT;
    self_test_state_t *self_test = &APP_CONTEXT.self_test;
    ota_state_t *ota = &APP_CONTEXT.ota;
    wifi_state_t *wifi = &APP_CONTEXT.wifi;
    power_module_t *pwr = &APP_CONTEXT.power;
    stats_module_t *stats = &APP_CONTEXT.stats;
    stratum_module_t *strat = &APP_CONTEXT.stratum;

    if (self_test->active) {

        screen_show(SCR_SELF_TEST);

        lv_label_set_text(self_test_message_label, self_test->message);

        if (self_test->finished) {
            if (self_test->result) {
                lv_label_set_text(self_test_result_label, "TESTS PASS!");
                lv_obj_remove_flag(self_test_finished_label_pass, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_label_set_text(self_test_result_label, "TESTS FAIL!");
                lv_obj_remove_flag(self_test_finished_label_fail, LV_OBJ_FLAG_HIDDEN);
            }
        }

        return;
    }

    if (ota->is_updating) {
        if (strcmp(ota->filename, lv_label_get_text(firmware_update_scr_filename_label)) != 0) {
            lv_label_set_text(firmware_update_scr_filename_label, ota->filename);
        }
        if (strcmp(ota->status, lv_label_get_text(firmware_update_scr_status_label)) != 0) {
            lv_label_set_text(firmware_update_scr_status_label, ota->status);
        }
        screen_show(SCR_FIRMWARE_UPDATE);
        return;
    }

    if (APP_CONTEXT.asic_status) {
        lv_label_set_text(asic_status_label, APP_CONTEXT.asic_status);
        screen_show(SCR_ASIC_STATUS);
        return;
    }

    if (pwr->overheat_mode == 1) {
        if (strcmp(wifi->ip_addr_str, lv_label_get_text(ip_addr_scr_overheat_label)) != 0) {
            lv_label_set_text(ip_addr_scr_overheat_label, wifi->ip_addr_str);
        }
        screen_show(SCR_OVERHEAT);
        return;
    }

    if (wifi->ssid[0] == '\0') {
        screen_show(SCR_CONFIGURE);
        return;
    }

    if (wifi->ap_enabled) {
        if (strcmp(wifi->wifi_status, lv_label_get_text(wifi_status_label)) != 0) {
            lv_label_set_text(wifi_status_label, wifi->wifi_status);
        }
        screen_show(SCR_CONNECTION);
        return;
    }

    current_screen_counter++;

    // Logo

    if (current_screen < SCR_LOGO) {
        screen_show(SCR_LOGO);
        return;
    }

    if (current_screen == SCR_LOGO) {
        if (LOGO_DELAY_COUNT > current_screen_counter) {
            return;
        }
        screen_show(SCR_CAROUSEL_START);
        return;
    }

    // Carousel

    char *pool_url = strat->is_using_fallback ? strat->fallback.url : strat->primary.url;
    if (strcmp(lv_label_get_text(mining_url_scr_urls_label), pool_url) != 0) {
        lv_label_set_text(mining_url_scr_urls_label, pool_url);
    }

    if (strcmp(lv_label_get_text(ip_addr_scr_urls_label), wifi->ip_addr_str) != 0) {
        lv_label_set_text(ip_addr_scr_urls_label, wifi->ip_addr_str);
    }

    if (current_hashrate != stats->hashrate) {
        lv_label_set_text_fmt(hashrate_label, "Gh/s: %.2f", stats->hashrate);
    }

    if (current_power != pwr->power || current_hashrate != stats->hashrate) {
        if (pwr->power > 0 && stats->hashrate > 0) {
            float efficiency = pwr->power / (stats->hashrate / 1000.0);
            lv_label_set_text_fmt(efficiency_label, "J/Th: %.2f", efficiency);
        }
    }

    if (stats->found_block && !found_block) {
        found_block = true;

        lv_obj_set_width(difficulty_label, LV_HOR_RES);
        lv_label_set_long_mode(difficulty_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text_fmt(difficulty_label, "Best: %s   !!! BLOCK FOUND !!!", stats->best_session_diff_string);

        screen_show(SCR_STATS);
    } else {
        if (current_difficulty != stats->best_session_nonce_diff) {
            lv_label_set_text_fmt(difficulty_label, "Best: %s/%s", stats->best_session_diff_string, stats->best_diff_string);
        }
    }

    if (current_chip_temp != pwr->chip_temp_avg && pwr->chip_temp_avg > 0) {
        lv_label_set_text_fmt(chip_temp_label, "Temp: %.1f C", pwr->chip_temp_avg);
    }

    current_hashrate = stats->hashrate;
    current_power = pwr->power;
    current_difficulty = stats->best_session_nonce_diff;
    current_chip_temp = pwr->chip_temp_avg;

    if (CAROUSEL_DELAY_COUNT > current_screen_counter || found_block) {
        return;
    }

    screen_next();
}

void screen_next()
{
    if (current_screen >= SCR_CAROUSEL_START) {
        screen_show(current_screen == SCR_CAROUSEL_END ? SCR_CAROUSEL_START : current_screen + 1);
    }
}

esp_err_t screen_start(void * pvParameters)
{
    (void)pvParameters;
    extern app_context_t APP_CONTEXT;

    if (APP_CONTEXT.is_screen_active) {
        wifi_state_t *wifi = &APP_CONTEXT.wifi;
        stratum_module_t *strat = &APP_CONTEXT.stratum;

        screens[SCR_SELF_TEST] = create_scr_self_test();
        screens[SCR_OVERHEAT] = create_scr_overheat(wifi);
        screens[SCR_ASIC_STATUS] = create_scr_asic_status();
        screens[SCR_CONFIGURE] = create_scr_configure(wifi);
        screens[SCR_FIRMWARE_UPDATE] = create_scr_ota();
        screens[SCR_CONNECTION] = create_scr_connection(wifi);
        screens[SCR_LOGO] = create_scr_logo();
        screens[SCR_URLS] = create_scr_urls(strat, wifi);
        screens[SCR_STATS] = create_scr_stats();

        lv_timer_create(screen_update_cb, SCREEN_UPDATE_MS, NULL);
    }

    return ESP_OK;
}
