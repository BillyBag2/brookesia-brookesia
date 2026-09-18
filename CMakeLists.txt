cmake_minimum_required(VERSION 3.22)

# Load generated Board Manager capabilities before project defaults so the
# selected board file can enable Brookesia features that depend on them.
set(SDKCONFIG_DEFAULTS "")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/components/gen_bmgr_codes/board_manager.defaults")
    list(APPEND SDKCONFIG_DEFAULTS
        "components/gen_bmgr_codes/board_manager.defaults"
    )
endif()

# Load common and chip defaults, followed by exactly one board's defaults.
list(APPEND SDKCONFIG_DEFAULTS
    "sdkconfig.default"
    "sdkconfig.esp32p4"
    "sdkconfig.m5_stack_tab5"
    # "sdkconfig.waveshare_esp32_p4_86_box"
    # "sdkconfig.m5_stack_esp32_p4_poe"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Brookesia-Brookesia)

# Package Brookesia's staged UI resources into the runtime filesystem and
# include that image in the normal `idf.py flash` command.
get_property(brookesia_resource_targets GLOBAL PROPERTY BROOKESIA_SYSTEM_SUPER_STAGE_TARGETS)
get_property(brookesia_app_targets GLOBAL PROPERTY BROOKESIA_RUNTIME_APP_STAGE_TARGETS)

# Apply the complete sparse, project-owned override tree after managed
# components stage their baseline resources. The WASM build calls the same
# helper with its own staging destination.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/resource_overrides.cmake")
brookesia_add_resource_overrides_target(apply_resource_overrides
    SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/resource_overrides"
    SYSTEM_ROOT "${CMAKE_CURRENT_LIST_DIR}/littlefs"
    LITTLEFS_ROOT "${CMAKE_CURRENT_LIST_DIR}/littlefs"
    DEPENDS ${brookesia_resource_targets} ${brookesia_app_targets})
littlefs_create_partition_image(
    littlefs_data
    "${CMAKE_CURRENT_LIST_DIR}/littlefs"
    FLASH_IN_PROJECT
    DEPENDS apply_resource_overrides
)
