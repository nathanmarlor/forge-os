idf_component_register(
SRCS 
    "bm1370.c"
   
    "serial.c"
    "crc.c"
    "common.c"
    "asic.c"
    "frequency_transition_bmXX.c"

INCLUDE_DIRS 
    "include"

REQUIRES
    "freertos"
    "esp_driver_gpio"
    "esp_driver_uart"
    "stratum"
    "event_bus"
)


# Include the header files from "main" directory
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main")

# Include the header files from "main/tasks" directory
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main/tasks")

# Include module headers
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/asic")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/config")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/stats")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/power")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../main/modules/stratum")
