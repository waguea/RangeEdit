add_executable(rangeedit
    src/main.cpp
)

target_link_libraries(rangeedit PRIVATE
    rangeedit_core
    rangeedit_warnings
)

if(RANGEEDIT_BUILD_TESTS)
    add_test(
        NAME CliTest
        COMMAND "${CMAKE_COMMAND}"
            "-DRANGEEDIT_EXECUTABLE=$<TARGET_FILE:rangeedit>"
            "-DCLI_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli_test_data"
            "-DCLI_TEST_REGEX=${RANGEEDIT_BUILD_REGEX}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.cmake"
    )
endif()
