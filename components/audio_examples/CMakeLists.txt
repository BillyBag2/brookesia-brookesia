idf_component_register(
    SRCS "audio_examples.cpp"
    INCLUDE_DIRS "."
    REQUIRES espressif__brookesia_system_core espressif__brookesia_service_audio
             espressif__brookesia_service_helper espressif__brookesia_hal_interface
             espressif__esp_audio_codec
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
target_link_libraries(${COMPONENT_LIB} PUBLIC
    "-u tab5_music_player_provider_symbol"
    "-u tab5_spectrum_analyser_provider_symbol"
)

idf_component_get_property(brookesia_system_core_dir espressif__brookesia_system_core COMPONENT_DIR)
include("${brookesia_system_core_dir}/cmake/runtime_app_stage.cmake")
brookesia_system_core_get_esp_runtime_app_stage_root(audio_example_stage_root)
brookesia_stage_runtime_app_package(
    PACKAGE_ID "brookesia.example.music_player"
    SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/package/music_player"
    STAGE_ROOT "${audio_example_stage_root}"
    NO_INDEX
)
