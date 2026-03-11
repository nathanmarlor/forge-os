#include "unity.h"
#include "global_state.h"
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

TEST_CASE("asic_module_init leaves jobs_lock NULL before bridge", "[asic]")
{
    asic_module_t m;
    asic_module_init(&m, 500.0, 256);

    TEST_ASSERT_NULL(m.jobs_lock);
}

// ── bridge tests ───────────────────────────────────────────────────────

TEST_CASE("asic_module_bridge_legacy aliases arrays", "[asic]")
{
    asic_module_t m;
    asic_module_init(&m, 500.0, 256);

    GlobalState gs;
    memset(&gs, 0, sizeof(gs));
    asic_module_bridge_legacy(&m, &gs);

    // GlobalState pointers should point to module's arrays
    TEST_ASSERT_EQUAL_PTR(m.valid_jobs, gs.valid_jobs);
    TEST_ASSERT_EQUAL_PTR(m.active_jobs, gs.ASIC_TASK_MODULE.active_jobs);
    TEST_ASSERT_EQUAL_PTR(m.dispatch_semaphore, gs.ASIC_TASK_MODULE.semaphore);
}

TEST_CASE("asic_module_bridge_legacy sets jobs_lock pointer", "[asic]")
{
    asic_module_t m;
    asic_module_init(&m, 500.0, 256);

    GlobalState gs;
    memset(&gs, 0, sizeof(gs));
    asic_module_bridge_legacy(&m, &gs);

    TEST_ASSERT_NOT_NULL(m.jobs_lock);
    TEST_ASSERT_EQUAL_PTR(&gs.valid_jobs_lock, m.jobs_lock);
}

TEST_CASE("asic_module writes through bridge are visible", "[asic]")
{
    asic_module_t m;
    asic_module_init(&m, 500.0, 256);

    GlobalState gs;
    memset(&gs, 0, sizeof(gs));
    asic_module_bridge_legacy(&m, &gs);

    // Write through module, read through GlobalState
    m.valid_jobs[42] = 1;
    TEST_ASSERT_EQUAL_UINT8(1, gs.valid_jobs[42]);

    // Write through GlobalState, read through module
    gs.valid_jobs[7] = 1;
    TEST_ASSERT_EQUAL_UINT8(1, m.valid_jobs[7]);
}

TEST_CASE("asic_module mutex works through bridge", "[asic]")
{
    asic_module_t m;
    asic_module_init(&m, 500.0, 256);

    GlobalState gs;
    memset(&gs, 0, sizeof(gs));
    asic_module_bridge_legacy(&m, &gs);

    // Lock via module pointer, unlock via GlobalState
    TEST_ASSERT_EQUAL(0, pthread_mutex_lock(m.jobs_lock));
    TEST_ASSERT_EQUAL(0, pthread_mutex_unlock(&gs.valid_jobs_lock));
}
