# asic_testable: test-only component wrapping asic module source
idf_component_register(
    SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../main/modules/asic/asic_module.c"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../../../main/modules/asic"
    REQUIRES asic esp_timer
)
