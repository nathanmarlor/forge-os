#include "unity.h"
#include "asic_module.h"
#include <string.h>

// ── init tests ─────────────────────────────────────────────────────────

TEST_CASE("asic_module_init zeroes job arrays", "[asic]")
{
    asic_module_t m;
    memset(&m, 0xFF, sizeof(m)); // dirty

    asic_module_init(&m, 500.0, 256);

    TEST_ASSERT_TRUE(m.initialized);
    for (int i = 0; i < ASIC_JOB_SLOTS; i++) {
        TEST_ASSERT_NULL(m.active_jobs[i]);
        TEST_ASSERT_EQUAL_UINT8(0, m.valid_jobs[i]);
    }
}

TEST_CASE("asic_module_init sets config values", "[asic]")
{
    asic_module_t m;
    asic_module_init(&m, 500.0, 256);

    TEST_ASSERT_FLOAT_WITHIN(0.1, 500.0, m.job_interval_ms);
    TEST_ASSERT_EQUAL_UINT32(256, m.asic_difficulty);
}

TEST_CASE("asic_module_init creates dispatch semaphore", "[asic]")
{
    asic_module_t m;
    asic_module_init(&m, 500.0, 256);

    TEST_ASSERT_NOT_NULL(m.dispatch_semaphore);
    // Semaphore starts empty (binary)
    TEST_ASSERT_EQUAL(pdFALSE, xSemaphoreTake(m.dispatch_semaphore, 0));
}

TEST_CASE("asic_module_init sets jobs_lock to owned mutex", "[asic]")
{
    asic_module_t m;
    asic_module_init(&m, 500.0, 256);

    TEST_ASSERT_EQUAL_PTR(&m.jobs_mutex, m.jobs_lock);
}

TEST_CASE("asic_module mutex lock/unlock works", "[asic]")
{
    asic_module_t m;
    asic_module_init(&m, 500.0, 256);

    TEST_ASSERT_EQUAL(0, pthread_mutex_lock(m.jobs_lock));
    TEST_ASSERT_EQUAL(0, pthread_mutex_unlock(m.jobs_lock));
}
