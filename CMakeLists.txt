cmake_minimum_required(VERSION 3.22)

# Load common and chip defaults, followed by exactly one board's defaults.
set(SDKCONFIG_DEFAULTS
    "sdkconfig.default"
    "sdkconfig.esp32p4"
    "sdkconfig.m5_stack_tab5"
    # "sdkconfig.waveshare_esp32_p4_86_box"
    # "sdkconfig.m5_stack_esp32_p4_poe"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sample_project)
