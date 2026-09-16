idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        brookesia_board_target
        espressif__esp-boost
        espressif__brookesia_gui_lvgl
        espressif__brookesia_lib_utils
        espressif__brookesia_service_helper
        espressif__brookesia_service_manager
        espressif__brookesia_system_super
        espressif__esp_lvgl_adapter
        lvgl__lvgl
)
