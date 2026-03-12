# CI wrapper for test/ — paths adjusted for test-ci/ working directory
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "../components" "../test/components")

set(TEST_COMPONENTS "bm1366 stratum event_bus stats_testable stratum_testable asic_testable" CACHE STRING "List of components to test")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(IDF_TARGET "esp32s3")

idf_build_set_property(COMPILE_DEFINITIONS "-DCONFIG_ASIC_FREQUENCY=100" APPEND)

project(unit_test_stratum)
