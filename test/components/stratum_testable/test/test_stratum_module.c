#include "unity.h"
#include "stratum_module.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

// ── helpers ────────────────────────────────────────────────────────────

static pool_config_t make_pool(const char *url, uint16_t port,
                               const char *user, const char *pass)
{
    return (pool_config_t){
        .url = (char *)url,
        .port = port,
        .username = (char *)user,
        .password = (char *)pass,
        .suggested_difficulty = 512,
        .extranonce_subscribe = true,
        .decode_coinbase = false,
    };
}

// ── init tests ─────────────────────────────────────────────────────────

TEST_CASE("stratum_module_init zeroes struct and sets defaults", "[stratum]")
{
    stratum_module_t m;
    memset(&m, 0xFF, sizeof(m)); // dirty

    pool_config_t pri = make_pool("pool.example.com", 3333, "worker1", "x");
    pool_config_t fb  = make_pool("fallback.example.com", 3334, "worker2", "y");
    stratum_module_init(&m, &pri, &fb);

    TEST_ASSERT_TRUE(m.initialized);
    TEST_ASSERT_EQUAL_UINT32(8192, m.stratum_difficulty);
    TEST_ASSERT_FALSE(m.is_using_fallback);
    TEST_ASSERT_EQUAL_UINT32(0, m.version_mask);
    TEST_ASSERT_NULL(m.extranonce_str);
    TEST_ASSERT_EQUAL_INT(0, m.extranonce_2_len);
}

TEST_CASE("stratum_module_init copies primary pool config", "[stratum]")
{
    stratum_module_t m;
    pool_config_t pri = make_pool("pool.example.com", 3333, "worker1", "x");
    stratum_module_init(&m, &pri, NULL);

    TEST_ASSERT_EQUAL_STRING("pool.example.com", m.primary.url);
    TEST_ASSERT_EQUAL_UINT16(3333, m.primary.port);
    TEST_ASSERT_EQUAL_STRING("worker1", m.primary.username);
    TEST_ASSERT_EQUAL_STRING("x", m.primary.password);
    TEST_ASSERT_EQUAL_UINT16(512, m.primary.suggested_difficulty);
    TEST_ASSERT_TRUE(m.primary.extranonce_subscribe);
    TEST_ASSERT_FALSE(m.primary.decode_coinbase);
}

TEST_CASE("stratum_module_init copies fallback pool config", "[stratum]")
{
    stratum_module_t m;
    pool_config_t pri = make_pool("pool.example.com", 3333, "worker1", "x");
    pool_config_t fb  = make_pool("fallback.example.com", 3334, "worker2", "y");
    stratum_module_init(&m, &pri, &fb);

    TEST_ASSERT_EQUAL_STRING("fallback.example.com", m.fallback.url);
    TEST_ASSERT_EQUAL_UINT16(3334, m.fallback.port);
    TEST_ASSERT_EQUAL_STRING("worker2", m.fallback.username);
}

TEST_CASE("stratum_module_init with NULL fallback zeroes fallback", "[stratum]")
{
    stratum_module_t m;
    pool_config_t pri = make_pool("pool.example.com", 3333, "worker1", "x");
    stratum_module_init(&m, &pri, NULL);

    TEST_ASSERT_NULL(m.fallback.url);
    TEST_ASSERT_EQUAL_UINT16(0, m.fallback.port);
}

// ── active pool tests ──────────────────────────────────────────────────

TEST_CASE("stratum_get_active_pool returns primary by default", "[stratum]")
{
    stratum_module_t m;
    pool_config_t pri = make_pool("primary.com", 3333, "w1", "x");
    pool_config_t fb  = make_pool("fallback.com", 3334, "w2", "y");
    stratum_module_init(&m, &pri, &fb);

    const pool_config_t *active = stratum_get_active_pool(&m);
    TEST_ASSERT_EQUAL_STRING("primary.com", active->url);
}

TEST_CASE("stratum_get_active_pool returns fallback when flag set", "[stratum]")
{
    stratum_module_t m;
    pool_config_t pri = make_pool("primary.com", 3333, "w1", "x");
    pool_config_t fb  = make_pool("fallback.com", 3334, "w2", "y");
    stratum_module_init(&m, &pri, &fb);

    m.is_using_fallback = true;
    const pool_config_t *active = stratum_get_active_pool(&m);
    TEST_ASSERT_EQUAL_STRING("fallback.com", active->url);
}

// ── RTT tracking tests ─────────────────────────────────────────────────

TEST_CASE("stratum_rtt_start records timestamp", "[stratum]")
{
    stratum_module_t m;
    pool_config_t pri = make_pool("p.com", 3333, "w", "x");
    stratum_module_init(&m, &pri, NULL);

    TEST_ASSERT_EQUAL_INT64(0, m.rtt.last_submit_us);
    stratum_rtt_start(&m);
    TEST_ASSERT_TRUE(m.rtt.last_submit_us > 0);
}

TEST_CASE("stratum_rtt_record with no start does nothing", "[stratum]")
{
    stratum_module_t m;
    pool_config_t pri = make_pool("p.com", 3333, "w", "x");
    stratum_module_init(&m, &pri, NULL);

    // last_submit_us == 0 after init, so record should be a no-op
    stratum_rtt_record(&m);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, m.rtt.ema);
    TEST_ASSERT_EQUAL_UINT8(0, m.rtt.sample_count);
}

TEST_CASE("stratum_rtt_record first sample sets EMA directly", "[stratum]")
{
    stratum_module_t m;
    pool_config_t pri = make_pool("p.com", 3333, "w", "x");
    stratum_module_init(&m, &pri, NULL);

    // Simulate: start, small delay, record
    stratum_rtt_start(&m);
    // Busy-wait ~1ms for a measurable RTT
    int64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < 1000) { }

    stratum_rtt_record(&m);

    TEST_ASSERT_TRUE(m.rtt.ema > 0.0f);  // Should be ~1ms
    TEST_ASSERT_EQUAL_UINT8(1, m.rtt.sample_count);
    TEST_ASSERT_EQUAL_UINT8(1, m.rtt.sample_idx);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, m.rtt.ema, m.rtt.min);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, m.rtt.ema, m.rtt.max);
}

TEST_CASE("stratum_rtt_record applies EMA smoothing on subsequent samples", "[stratum]")
{
    stratum_module_t m;
    pool_config_t pri = make_pool("p.com", 3333, "w", "x");
    stratum_module_init(&m, &pri, NULL);

    // First sample
    stratum_rtt_start(&m);
    int64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < 1000) { }
    stratum_rtt_record(&m);

    float first_ema = m.rtt.ema;

    // Second sample with different delay
    stratum_rtt_start(&m);
    start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < 2000) { }
    stratum_rtt_record(&m);

    // EMA should have shifted toward the new (longer) sample
    TEST_ASSERT_TRUE(m.rtt.ema > first_ema);
    TEST_ASSERT_EQUAL_UINT8(2, m.rtt.sample_count);
}

TEST_CASE("stratum_rtt_record tracks min and max", "[stratum]")
{
    stratum_module_t m;
    pool_config_t pri = make_pool("p.com", 3333, "w", "x");
    stratum_module_init(&m, &pri, NULL);

    // Short sample
    stratum_rtt_start(&m);
    int64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < 500) { }
    stratum_rtt_record(&m);
    float short_rtt = m.rtt.min;

    // Long sample
    stratum_rtt_start(&m);
    start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < 5000) { }
    stratum_rtt_record(&m);

    TEST_ASSERT_TRUE(m.rtt.max > m.rtt.min);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, short_rtt, m.rtt.min); // min unchanged
}

TEST_CASE("stratum_rtt circular buffer wraps at SAMPLE_COUNT", "[stratum]")
{
    stratum_module_t m;
    pool_config_t pri = make_pool("p.com", 3333, "w", "x");
    stratum_module_init(&m, &pri, NULL);

    // Fill buffer beyond capacity by manually injecting
    for (int i = 0; i < STRATUM_RTT_SAMPLE_COUNT + 5; i++) {
        stratum_rtt_start(&m);
        int64_t start = esp_timer_get_time();
        while ((esp_timer_get_time() - start) < 100) { }
        stratum_rtt_record(&m);
    }

    // sample_count should cap at STRATUM_RTT_SAMPLE_COUNT
    TEST_ASSERT_EQUAL_UINT8(STRATUM_RTT_SAMPLE_COUNT, m.rtt.sample_count);
    // sample_idx should have wrapped
    TEST_ASSERT_EQUAL_UINT8(5, m.rtt.sample_idx);
}

// ── protocol state tests ───────────────────────────────────────────────

TEST_CASE("stratum module protocol state is zeroed on init", "[stratum]")
{
    stratum_module_t m;
    memset(&m, 0xFF, sizeof(m));
    pool_config_t pri = make_pool("p.com", 3333, "w", "x");
    stratum_module_init(&m, &pri, NULL);

    TEST_ASSERT_EQUAL_UINT32(0, m.version_mask);
    TEST_ASSERT_EQUAL_INT(0, m.block_height);
    TEST_ASSERT_EQUAL_UINT64(0, m.network_nonce_diff);
    TEST_ASSERT_EQUAL_UINT8(0, m.scriptsig[0]);
}
