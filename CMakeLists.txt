cmake_minimum_required(VERSION 3.24)

project(RangeEdit
    VERSION 0.1.0
    DESCRIPTION "High-performance text transformation engine and VS Code extension"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RANGEEDIT_BUILD_TESTS "Build unit tests" ON)
option(RANGEEDIT_BUILD_BENCHMARKS "Build microbenchmarks" ON)
option(RANGEEDIT_BUILD_CLI "Build the rangeedit CLI" ON)
option(RANGEEDIT_BUILD_REGEX "Build RE2-backed regex matching" ON)
option(RANGEEDIT_BUILD_ADDON "Build the Node-API addon (requires Node headers)" OFF)
option(RANGEEDIT_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(RANGEEDIT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(rangeedit_warnings INTERFACE)

if(MSVC)
    target_compile_options(rangeedit_warnings INTERFACE /W4 /permissive-)
    if(RANGEEDIT_WARNINGS_AS_ERRORS)
        target_compile_options(rangeedit_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(rangeedit_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wnull-dereference)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(rangeedit_warnings INTERFACE -Wno-nullability-extension)
    endif()
    if(RANGEEDIT_WARNINGS_AS_ERRORS)
        target_compile_options(rangeedit_warnings INTERFACE -Werror)
    endif()
endif()

if(RANGEEDIT_ENABLE_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()

add_subdirectory(engine)

if(RANGEEDIT_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(RANGEEDIT_BUILD_ADDON)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/addon/CMakeLists.txt")
        message(FATAL_ERROR "RANGEEDIT_BUILD_ADDON=ON but addon/CMakeLists.txt does not exist yet")
    endif()
    add_subdirectory(addon)
endif()
