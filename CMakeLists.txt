cmake_minimum_required(VERSION 3.20)
project(AtlasOrderBook VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -O3
    )
endif()

# Enable Sanitizers option
option(ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(ENABLE_ASAN)
    message(STATUS "Enabling ASan and UBSan")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Core library (interface and header-only modules)
add_library(atlas_core INTERFACE)
target_include_directories(atlas_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Unit & Invariant Tests
add_executable(atlas_tests
    tests/test_main.cpp
    tests/test_unit.cpp
    tests/test_differential.cpp
)
target_link_libraries(atlas_tests PRIVATE atlas_core GTest::gtest GTest::gtest_main)

# Example Demo executable
add_executable(atlas_demo
    examples/demo.cpp
)
target_link_libraries(atlas_demo PRIVATE atlas_core)

# Replay Tool executable
add_executable(atlas_replay
    replay/replay_main.cpp
)
target_link_libraries(atlas_replay PRIVATE atlas_core)

# Benchmark executable
add_executable(atlas_benchmark
    benchmarks/main_bench.cpp
)
target_link_libraries(atlas_benchmark PRIVATE atlas_core)

enable_testing()
add_test(NAME atlas_all_tests COMMAND atlas_tests)
