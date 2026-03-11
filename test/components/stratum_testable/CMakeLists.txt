# stratum_testable: test-only component wrapping stratum module source
idf_component_register(
    SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../main/modules/stratum/stratum_module.c"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../../../main/modules/stratum"
    REQUIRES esp_timer
)
