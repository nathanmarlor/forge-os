# asic_testable: test-only component wrapping asic module source
# Mocks GlobalState with a minimal stub header.
idf_component_register(
    SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../main/modules/asic/asic_module.c"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../../../main/modules/asic"
        "mocks"
    REQUIRES asic esp_timer
)

# Mock global_state.h must be found BEFORE any real global_state.h
target_include_directories(${COMPONENT_LIB} BEFORE PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/mocks"
)
