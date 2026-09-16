set(BROOKESIA_BOARD_METADATA
    "${CMAKE_CURRENT_LIST_DIR}/../gen_bmgr_codes/gen_board_metadata.yaml")
if(NOT EXISTS "${BROOKESIA_BOARD_METADATA}")
    message(FATAL_ERROR
        "Board Manager metadata is missing: ${BROOKESIA_BOARD_METADATA}. "
        "Regenerate components/gen_bmgr_codes before configuring.")
endif()

file(STRINGS "${BROOKESIA_BOARD_METADATA}" BROOKESIA_BOARD_METADATA_LINE
    REGEX "^board:[ ]*" LIMIT_COUNT 1)
string(REGEX REPLACE "^board:[ ]*" "" BROOKESIA_BOARD_TARGET
    "${BROOKESIA_BOARD_METADATA_LINE}")
string(STRIP "${BROOKESIA_BOARD_TARGET}" BROOKESIA_BOARD_TARGET)

if(BROOKESIA_BOARD_TARGET STREQUAL "")
    message(FATAL_ERROR
        "No board name was found in ${BROOKESIA_BOARD_METADATA}")
endif()

set(BROOKESIA_BOARD_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../boards/${BROOKESIA_BOARD_TARGET}")
if(NOT EXISTS "${BROOKESIA_BOARD_ROOT}/common/include/brookesia/board/config.hpp" OR
   NOT EXISTS "${BROOKESIA_BOARD_ROOT}/native/src/native.cpp")
    message(FATAL_ERROR
        "Board '${BROOKESIA_BOARD_TARGET}' has no complete Brookesia board profile at "
        "${BROOKESIA_BOARD_ROOT}")
endif()

idf_component_register(
    SRCS
        "${BROOKESIA_BOARD_ROOT}/native/src/native.cpp"
    INCLUDE_DIRS
        "${BROOKESIA_BOARD_ROOT}/common/include"
        "${BROOKESIA_BOARD_ROOT}/native/include"
    REQUIRES
        esp_board_manager
)

message(STATUS "Brookesia native board target: ${BROOKESIA_BOARD_TARGET}")
