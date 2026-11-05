find_program(RANGEEDIT_NODE_EXECUTABLE NAMES node nodejs REQUIRED)

if(NOT RANGEEDIT_NODE_INCLUDE_DIR)
    get_filename_component(_rangeedit_node_bin "${RANGEEDIT_NODE_EXECUTABLE}" DIRECTORY)
    get_filename_component(_rangeedit_node_prefix "${_rangeedit_node_bin}" DIRECTORY)

    find_path(RANGEEDIT_NODE_INCLUDE_DIR
        NAMES node_api.h
        HINTS
            "${_rangeedit_node_bin}/include/node"
            "${_rangeedit_node_prefix}/include/node"
            "${_rangeedit_node_prefix}/include"
        NO_DEFAULT_PATH)

    if(NOT RANGEEDIT_NODE_INCLUDE_DIR)
        find_path(RANGEEDIT_NODE_INCLUDE_DIR NAMES node_api.h)
    endif()
endif()

if(NOT RANGEEDIT_NODE_INCLUDE_DIR)
    message(FATAL_ERROR "Node-API headers not found; set RANGEEDIT_NODE_INCLUDE_DIR")
endif()

if(WIN32 AND NOT RANGEEDIT_NODE_LIBRARY)
    find_library(RANGEEDIT_NODE_LIBRARY
        NAMES node libnode
        HINTS
            "${_rangeedit_node_bin}"
            "${_rangeedit_node_prefix}/lib"
        NO_DEFAULT_PATH)
    if(NOT RANGEEDIT_NODE_LIBRARY)
        message(FATAL_ERROR "Node import library not found; set RANGEEDIT_NODE_LIBRARY")
    endif()
endif()

add_library(rangeedit_addon MODULE src/addon.cpp)

target_compile_definitions(rangeedit_addon PRIVATE NAPI_VERSION=8)
target_include_directories(rangeedit_addon SYSTEM PRIVATE "${RANGEEDIT_NODE_INCLUDE_DIR}")
target_link_libraries(rangeedit_addon PRIVATE rangeedit_core rangeedit_warnings)

if(APPLE)
    target_link_options(rangeedit_addon PRIVATE "-undefined" "dynamic_lookup")
elseif(WIN32)
    target_link_libraries(rangeedit_addon PRIVATE "${RANGEEDIT_NODE_LIBRARY}")
endif()

set_target_properties(rangeedit_addon PROPERTIES
    PREFIX ""
    SUFFIX ".node"
    OUTPUT_NAME "rangeedit"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(RANGEEDIT_BUILD_TESTS)
    add_test(
        NAME AddonTest
        COMMAND
            ${CMAKE_COMMAND} -E env
            "RANGEEDIT_ADDON=$<TARGET_FILE:rangeedit_addon>"
            ${RANGEEDIT_NODE_EXECUTABLE} --test
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/addon_test.mjs"
    )
    set_tests_properties(AddonTest PROPERTIES TIMEOUT 120)
endif()
