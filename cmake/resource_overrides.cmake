include_guard(GLOBAL)

function(brookesia_add_resource_overrides_target target_name)
    set(one_value_args SOURCE_DIR SYSTEM_ROOT LITTLEFS_ROOT)
    set(multi_value_args DEPENDS)
    cmake_parse_arguments(overrides "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT overrides_SOURCE_DIR OR NOT IS_DIRECTORY "${overrides_SOURCE_DIR}")
        message(FATAL_ERROR "Resource override directory not found: ${overrides_SOURCE_DIR}")
    endif()
    if(NOT overrides_SYSTEM_ROOT OR NOT overrides_LITTLEFS_ROOT)
        message(FATAL_ERROR "System and LittleFS override roots must be provided")
    endif()

    file(GLOB_RECURSE override_files CONFIGURE_DEPENDS
        LIST_DIRECTORIES FALSE
        "${overrides_SOURCE_DIR}/*")
    # SystemSuper uses a platform-level system root on WASM, whereas apps and
    # other persistent resources live below the LittleFS mount. Native firmware
    # passes the same directory for both roots.
    add_custom_target(${target_name}
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${overrides_SYSTEM_ROOT}/system"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${overrides_SOURCE_DIR}/system" "${overrides_SYSTEM_ROOT}/system"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${overrides_LITTLEFS_ROOT}/apps"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${overrides_SOURCE_DIR}/apps" "${overrides_LITTLEFS_ROOT}/apps"
        COMMENT "Applying project LittleFS resource overrides"
        VERBATIM)
    if(overrides_DEPENDS)
        add_dependencies(${target_name} ${overrides_DEPENDS})
    endif()

    set(${target_name}_FILES "${override_files}" PARENT_SCOPE)
endfunction()
