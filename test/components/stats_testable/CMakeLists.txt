# stats_testable: a test-only component wrapping the stats module source
# This provides stats.h and the compiled stats.c for use in tests,
# with NVS functions mocked via shadowed headers.
idf_component_register(
    SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../main/modules/stats/stats.c"
         "mocks/mock_nvs.c"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../../../main/modules/stats"
        "mocks"
    REQUIRES asic esp_timer
)

# Mock nvs_config.h must be found BEFORE any real nvs_config.h
target_include_directories(${COMPONENT_LIB} BEFORE PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/mocks"
)
