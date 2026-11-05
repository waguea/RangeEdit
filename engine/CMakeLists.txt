include(FetchContent)

set(RANGEEDIT_CORE_SOURCES
    src/edit.cpp
    src/literal.cpp
    src/stream.cpp
)

if(RANGEEDIT_BUILD_REGEX)
    FetchContent_Declare(
        abseil
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG 20250814.1
        GIT_SHALLOW TRUE
    )
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(abseil)

    FetchContent_Declare(
        re2
        GIT_REPOSITORY https://github.com/google/re2.git
        GIT_TAG 2025-11-05
        GIT_SHALLOW TRUE
    )
    set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(RE2_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(re2)

    set(RANGEEDIT_RE2_TARGET re2::re2)
    list(APPEND RANGEEDIT_CORE_SOURCES src/regex.cpp)
else()
    list(APPEND RANGEEDIT_CORE_SOURCES src/regex_stub.cpp)
endif()

add_library(rangeedit_core STATIC ${RANGEEDIT_CORE_SOURCES})

add_library(RangeEdit::core ALIAS rangeedit_core)

target_include_directories(rangeedit_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(rangeedit_core PUBLIC cxx_std_20)
target_link_libraries(rangeedit_core PRIVATE rangeedit_warnings)

if(RANGEEDIT_BUILD_REGEX)
    target_link_libraries(rangeedit_core PRIVATE ${RANGEEDIT_RE2_TARGET})
    target_compile_definitions(rangeedit_core PUBLIC RANGEEDIT_HAS_REGEX=1)
endif()

if(RANGEEDIT_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    set(RANGEEDIT_TEST_SOURCES
        tests/edit_test.cpp
        tests/literal_test.cpp
        tests/stream_test.cpp
    )

    if(RANGEEDIT_BUILD_REGEX)
        list(APPEND RANGEEDIT_TEST_SOURCES tests/regex_test.cpp)
    endif()

    add_executable(rangeedit_tests ${RANGEEDIT_TEST_SOURCES})

    target_link_libraries(rangeedit_tests PRIVATE
        rangeedit_core
        GTest::gtest_main
        rangeedit_warnings
    )

    include(GoogleTest)
    gtest_discover_tests(rangeedit_tests)
endif()

if(RANGEEDIT_BUILD_BENCHMARKS)
    if(NOT TARGET benchmark::benchmark)
        FetchContent_Declare(
            googlebenchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG v1.9.1
            GIT_SHALLOW TRUE
        )
        set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googlebenchmark)
    endif()

    add_executable(rangeedit_bench
        bench/literal_bench.cpp
    )

    target_link_libraries(rangeedit_bench PRIVATE
        rangeedit_core
        benchmark::benchmark
        rangeedit_warnings
    )
endif()
