#include <stdio.h>
#include <string.h>
#include "unity.h"

static void print_banner(const char *text);

void app_main(void)
{
    print_banner("Running all the registered tests");
    UNITY_BEGIN();
    unity_run_tests_by_tag("[not-on-qemu]", true);
    UNITY_END();

    // Flush stdout to ensure QEMU serial file output captures the Unity summary
    fflush(stdout);

    // Small delay to allow UART FIFO to drain before exit
    for (volatile int i = 0; i < 1000000; i++) {}

    exit(0);
}

static void print_banner(const char *text)
{
    printf("\n#### %s #####\n\n", text);
}