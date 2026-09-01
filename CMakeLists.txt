cmake_minimum_required(VERSION 3.18)
project(Charge_Language)

if (NOT MSVC)
    set(CMAKE_BUILD_TYPE release)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -g")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mbmi -mbmi2 -mpopcnt -mlzcnt -msse4")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -falign-functions=64 -falign-loops=32 -mbranches-within-32B-boundaries")
    add_compile_definitions("CHARGE_SSE_OPTIMIZATIONS=1")
endif()

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        52eb8108c5bdec04579160ae17225d66034bd723 # v1.17.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
    cli11_proj
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        cbd58a3696887b34c70949aef21a71735a0c2ad5 # v2.7.2
)
set(CLI11_PRECOMPILED ON CACHE BOOL "" FORCE) # Build static library instead of header only
FetchContent_MakeAvailable(cli11_proj)

FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        04b5f41ec7e3b68b28a2379bc19804a48953117d # main on Aug 31 2026
)
set(BENCHMARK_ENABLE_LIBPFM on) # Enable performance counters
FetchContent_MakeAvailable(googlebenchmark)

add_subdirectory(compiler)