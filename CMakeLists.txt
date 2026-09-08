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
project(sample_project)

# Package Brookesia's staged UI resources into the runtime filesystem and
# include that image in the normal `idf.py flash` command.
get_property(brookesia_resource_targets GLOBAL PROPERTY BROOKESIA_SYSTEM_SUPER_STAGE_TARGETS)
littlefs_create_partition_image(
    littlefs_data
    "${CMAKE_CURRENT_LIST_DIR}/littlefs"
    FLASH_IN_PROJECT
    DEPENDS ${brookesia_resource_targets}
)
