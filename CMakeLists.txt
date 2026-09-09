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

# Apply project-owned Settings layout fixes after the managed component stages
# its package. Keeping these overrides outside managed_components makes them
# survive component refreshes and normal rebuilds.
set(settings_override_root
    "${CMAKE_CURRENT_LIST_DIR}/resource_overrides/apps/brookesia.general.settings/res"
)
set(settings_staged_root
    "${CMAKE_CURRENT_LIST_DIR}/littlefs/apps/brookesia.general.settings/res"
)
add_custom_target(apply_settings_resource_overrides
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${settings_override_root}/screens/header.json"
        "${settings_staged_root}/screens/header.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${settings_override_root}/constants/portrait.json"
        "${settings_staged_root}/constants/portrait.json"
    DEPENDS ${brookesia_resource_targets}
    VERBATIM
)
littlefs_create_partition_image(
    littlefs_data
    "${CMAKE_CURRENT_LIST_DIR}/littlefs"
    FLASH_IN_PROJECT
    DEPENDS apply_settings_resource_overrides
)
