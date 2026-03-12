#ifndef SELF_TEST_H_
#define SELF_TEST_H_

#include <stdbool.h>

typedef enum test_failed_cause
{
  NO_FAILURE = -1,
  PERIPHERAL_FAILURE = 0,
  ASIC_FAILURE = 10,
  POWER_FAILURE = 20
} TEST_FAILED_CAUSE;

void execute_production_test(void);
bool production_test(void);

typedef void (*self_test_progress_cb)(int step, const char *name, bool passed, const char *detail);
bool runtime_self_test(self_test_progress_cb cb);

#endif
