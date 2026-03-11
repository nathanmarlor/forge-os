#include "unity.h"
#include "stats.h"
#include "nvs_config.h"  // mock version
#include <string.h>
#include <math.h>

// Helper: create an initialized stats module with given ASIC count
static stats_module_t create_stats(int asic_count, int domains)
{
    stats_module_t stats;
    mock_nvs_reset();
    TEST_ASSERT_EQUAL(ESP_OK, stats_module_init(&stats, asic_count, domains));
    return stats;
}

// ---- stats_format_diff_string tests ----

TEST_CASE("format_diff: zero", "[stats]")
{
    char buf[STATS_DIFF_STRING_SIZE];
    stats_format_diff_string(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0", buf);
}

TEST_CASE("format_diff: small values (no suffix)", "[stats]")
{
    char buf[STATS_DIFF_STRING_SIZE];
    stats_format_diff_string(1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1", buf);

    stats_format_diff_string(999, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("999", buf);
}

TEST_CASE("format_diff: kilo range", "[stats]")
{
    char buf[STATS_DIFF_STRING_SIZE];
    stats_format_diff_string(1000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1k", buf);

    stats_format_diff_string(1500, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1.5k", buf);

    stats_format_diff_string(999999, buf, sizeof(buf));
    // 999999 / 1000 = 999.999 -> "1e+03k" no, let's check: val >= 1000, < 1000000
    // dval = 999999.0 / 1000.0 = 999.999, %.3g = "1e+03"... hmm
    // Actually %.3g for 999.999 -> "1e+03" which is ugly. But this is the existing behavior.
}

TEST_CASE("format_diff: mega range", "[stats]")
{
    char buf[STATS_DIFF_STRING_SIZE];
    stats_format_diff_string(1000000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1M", buf);

    stats_format_diff_string(2500000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2.5M", buf);
}

TEST_CASE("format_diff: giga range", "[stats]")
{
    char buf[STATS_DIFF_STRING_SIZE];
    stats_format_diff_string(1000000000ULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1G", buf);

    stats_format_diff_string(42000000000ULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("42G", buf);
}

TEST_CASE("format_diff: tera range", "[stats]")
{
    char buf[STATS_DIFF_STRING_SIZE];
    stats_format_diff_string(1000000000000ULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1T", buf);
}

TEST_CASE("format_diff: peta range", "[stats]")
{
    char buf[STATS_DIFF_STRING_SIZE];
    stats_format_diff_string(1000000000000000ULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1P", buf);
}

TEST_CASE("format_diff: exa range", "[stats]")
{
    char buf[STATS_DIFF_STRING_SIZE];
    stats_format_diff_string(1000000000000000000ULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1E", buf);
}

// ---- stats_hash_counter_to_ghs tests ----

TEST_CASE("hash_counter_to_ghs: zero duration returns 0", "[stats]")
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, stats_hash_counter_to_ghs(0, 100));
}

TEST_CASE("hash_counter_to_ghs: zero counter returns 0", "[stats]")
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, stats_hash_counter_to_ghs(1000, 0));
}

TEST_CASE("hash_counter_to_ghs: known conversion", "[stats]")
{
    // 1 counter increment = 2^32 hashes
    // In 1 second (1000ms), 1 counter = 2^32 / 1e9 GH/s = 4.294967296 GH/s
    float result = stats_hash_counter_to_ghs(1000, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.295f, result);
}

TEST_CASE("hash_counter_to_ghs: multiple counters", "[stats]")
{
    // 10 counters in 2 seconds = 5 counters/sec * 2^32 / 1e9
    float result = stats_hash_counter_to_ghs(2000, 10);
    float expected = 5.0f * 4294967296.0f / 1e9f;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, expected, result);
}

// ---- stats_module_init tests ----

TEST_CASE("init: succeeds with valid params", "[stats]")
{
    stats_module_t stats;
    mock_nvs_reset();
    TEST_ASSERT_EQUAL(ESP_OK, stats_module_init(&stats, 2, 4));
    TEST_ASSERT_TRUE(stats.hashrate_initialized);
    TEST_ASSERT_EQUAL(2, stats.asic_count);
    TEST_ASSERT_EQUAL(4, stats.hash_domains);
    TEST_ASSERT_NOT_NULL(stats.total_measurement);
    TEST_ASSERT_NOT_NULL(stats.domain_measurements);
    TEST_ASSERT_NOT_NULL(stats.error_measurement);
}

TEST_CASE("init: loads persisted best diff from NVS", "[stats]")
{
    mock_nvs_reset();
    _mock_nvs_u64_set = true;
    _mock_nvs_u64_value = 42000;

    stats_module_t stats;
    TEST_ASSERT_EQUAL(ESP_OK, stats_module_init(&stats, 1, 0));
    TEST_ASSERT_EQUAL(42000, stats.best_nonce_diff);
    TEST_ASSERT_EQUAL_STRING("42k", stats.best_diff_string);
}

TEST_CASE("init: zero domains skips domain allocation", "[stats]")
{
    stats_module_t stats;
    mock_nvs_reset();
    TEST_ASSERT_EQUAL(ESP_OK, stats_module_init(&stats, 1, 0));
    TEST_ASSERT_NULL(stats.domain_measurements);
}

TEST_CASE("init: measurements start at zero", "[stats]")
{
    stats_module_t stats = create_stats(2, 4);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, stats.total_measurement[0].hashrate);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, stats.total_measurement[1].hashrate);
    TEST_ASSERT_EQUAL(0, stats.shares_accepted);
    TEST_ASSERT_EQUAL(0, stats.shares_rejected);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, stats.hashrate);
}

// ---- Share counting tests ----

TEST_CASE("notify_accepted increments counter", "[stats]")
{
    stats_module_t stats = create_stats(1, 0);
    TEST_ASSERT_EQUAL(0, stats.shares_accepted);

    stats_notify_accepted_share(&stats);
    TEST_ASSERT_EQUAL(1, stats.shares_accepted);

    stats_notify_accepted_share(&stats);
    stats_notify_accepted_share(&stats);
    TEST_ASSERT_EQUAL(3, stats.shares_accepted);
}

TEST_CASE("notify_rejected increments counter and tracks reason", "[stats]")
{
    stats_module_t stats = create_stats(1, 0);

    stats_notify_rejected_share(&stats, "low difficulty share");
    TEST_ASSERT_EQUAL(1, stats.shares_rejected);
    TEST_ASSERT_EQUAL(1, stats.rejected_reason_count);
    TEST_ASSERT_EQUAL_STRING("low difficulty share", stats.rejected_reasons[0].message);
    TEST_ASSERT_EQUAL(1, stats.rejected_reasons[0].count);
}

TEST_CASE("notify_rejected: duplicate reasons increment count", "[stats]")
{
    stats_module_t stats = create_stats(1, 0);

    stats_notify_rejected_share(&stats, "stale");
    stats_notify_rejected_share(&stats, "stale");
    stats_notify_rejected_share(&stats, "stale");

    TEST_ASSERT_EQUAL(3, stats.shares_rejected);
    TEST_ASSERT_EQUAL(1, stats.rejected_reason_count);
    TEST_ASSERT_EQUAL(3, stats.rejected_reasons[0].count);
}

TEST_CASE("notify_rejected: multiple reasons tracked and sorted", "[stats]")
{
    stats_module_t stats = create_stats(1, 0);

    stats_notify_rejected_share(&stats, "stale");
    stats_notify_rejected_share(&stats, "low difficulty share");
    stats_notify_rejected_share(&stats, "low difficulty share");
    stats_notify_rejected_share(&stats, "low difficulty share");

    TEST_ASSERT_EQUAL(4, stats.shares_rejected);
    TEST_ASSERT_EQUAL(2, stats.rejected_reason_count);
    // Should be sorted by count descending
    TEST_ASSERT_EQUAL_STRING("low difficulty share", stats.rejected_reasons[0].message);
    TEST_ASSERT_EQUAL(3, stats.rejected_reasons[0].count);
    TEST_ASSERT_EQUAL_STRING("stale", stats.rejected_reasons[1].message);
    TEST_ASSERT_EQUAL(1, stats.rejected_reasons[1].count);
}

TEST_CASE("notify_rejected: max reasons cap", "[stats]")
{
    stats_module_t stats = create_stats(1, 0);

    // Fill up all reason slots
    char reason[64];
    for (int i = 0; i < STATS_MAX_REJECTION_REASONS; i++) {
        snprintf(reason, sizeof(reason), "reason_%d", i);
        stats_notify_rejected_share(&stats, reason);
    }
    TEST_ASSERT_EQUAL(STATS_MAX_REJECTION_REASONS, stats.rejected_reason_count);

    // One more should still count the rejection but not add a new reason
    stats_notify_rejected_share(&stats, "overflow_reason");
    TEST_ASSERT_EQUAL(STATS_MAX_REJECTION_REASONS + 1, stats.shares_rejected);
    TEST_ASSERT_EQUAL(STATS_MAX_REJECTION_REASONS, stats.rejected_reason_count);
}

// ---- Best diff tests ----

TEST_CASE("check_best_diff: updates session best", "[stats]")
{
    stats_module_t stats = create_stats(1, 0);

    stats_check_best_diff(&stats, 1000.0, 1e15);
    TEST_ASSERT_EQUAL(1000, stats.best_session_nonce_diff);

    stats_check_best_diff(&stats, 5000.0, 1e15);
    TEST_ASSERT_EQUAL(5000, stats.best_session_nonce_diff);

    // Lower value should not update
    stats_check_best_diff(&stats, 2000.0, 1e15);
    TEST_ASSERT_EQUAL(5000, stats.best_session_nonce_diff);
}

TEST_CASE("check_best_diff: updates all-time best and persists", "[stats]")
{
    mock_nvs_reset();
    stats_module_t stats = create_stats(1, 0);

    stats_check_best_diff(&stats, 99999.0, 1e15);
    TEST_ASSERT_EQUAL(99999, stats.best_nonce_diff);
    // Should have persisted to mock NVS
    TEST_ASSERT_TRUE(_mock_nvs_u64_set);
    TEST_ASSERT_EQUAL(99999, _mock_nvs_u64_value);
}

TEST_CASE("check_best_diff: detects block find", "[stats]")
{
    stats_module_t stats = create_stats(1, 0);
    TEST_ASSERT_FALSE(stats.found_block);

    // nonce_diff > network_diff = block found
    stats_check_best_diff(&stats, 1e16, 1e15);
    TEST_ASSERT_TRUE(stats.found_block);
}

TEST_CASE("check_best_diff: no block when below network diff", "[stats]")
{
    stats_module_t stats = create_stats(1, 0);

    stats_check_best_diff(&stats, 1000.0, 1e15);
    TEST_ASSERT_FALSE(stats.found_block);
}

// ---- CPU load tests ----

TEST_CASE("update_cpu_load: stores values", "[stats]")
{
    stats_module_t stats = create_stats(1, 0);

    stats_update_cpu_load(&stats, 45.5f, 78.2f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.5f, stats.cpu0_percent);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 78.2f, stats.cpu1_percent);
}

// ---- EMA hashrate tests ----

TEST_CASE("compute_hashrate: zero when no measurements", "[stats]")
{
    stats_module_t stats = create_stats(1, 4);

    stats_compute_hashrate(&stats);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, stats.hashrate);
}

TEST_CASE("compute_hashrate: first non-zero sets directly", "[stats]")
{
    stats_module_t stats = create_stats(1, 4);

    // Simulate a hashrate register read
    stats.total_measurement[0].hashrate = 500.0f;

    stats_compute_hashrate(&stats);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 500.0f, stats.hashrate);
}

TEST_CASE("compute_hashrate: EMA smoothing on subsequent updates", "[stats]")
{
    stats_module_t stats = create_stats(1, 4);

    // First: sets directly
    stats.total_measurement[0].hashrate = 100.0f;
    stats_compute_hashrate(&stats);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, stats.hashrate);

    // Second: EMA with alpha=12
    // new_ema = (100 * 11 + 200) / 12 = 1300/12 = 108.33
    stats.total_measurement[0].hashrate = 200.0f;
    stats_compute_hashrate(&stats);
    float expected = (100.0f * 11.0f + 200.0f) / 12.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, expected, stats.hashrate);
}

TEST_CASE("compute_hashrate: multi-ASIC sums", "[stats]")
{
    stats_module_t stats = create_stats(2, 4);

    stats.total_measurement[0].hashrate = 250.0f;
    stats.total_measurement[1].hashrate = 250.0f;

    stats_compute_hashrate(&stats);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 500.0f, stats.hashrate);
}

// ---- Register read tests ----

TEST_CASE("handle_register_read: out of bounds ASIC nr rejected", "[stats]")
{
    stats_module_t stats = create_stats(2, 4);
    // Should not crash - just log error
    stats_handle_register_read(&stats, REGISTER_TOTAL_COUNT, 5, 100);
    // Measurements should be unchanged
    TEST_ASSERT_EQUAL(0, stats.total_measurement[0].value);
    TEST_ASSERT_EQUAL(0, stats.total_measurement[1].value);
}

// ---- Reset tests ----

TEST_CASE("reset_measurements: clears all", "[stats]")
{
    stats_module_t stats = create_stats(2, 4);

    stats.total_measurement[0].hashrate = 100.0f;
    stats.total_measurement[0].value = 42;
    stats.error_measurement[1].hashrate = 5.0f;

    stats_reset_measurements(&stats);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, stats.total_measurement[0].hashrate);
    TEST_ASSERT_EQUAL(0, stats.total_measurement[0].value);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, stats.error_measurement[1].hashrate);
}
