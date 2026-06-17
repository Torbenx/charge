cmake_minimum_required(VERSION 3.18)
project(Charge_Language)

if (NOT MSVC)
    set(CMAKE_BUILD_TYPE release)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -g")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mbmi -mbmi2 -mpopcnt -mlzcnt")
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
    ska_hash_maps
    GIT_REPOSITORY https://github.com/skarupke/flat_hash_map.git
    GIT_TAG        2c4687431f978f02a3780e24b8b701d22aa32d9c
)
FetchContent_MakeAvailable(ska_hash_maps)
FetchContent_GetProperties(ska_hash_maps SOURCE_DIR SKA_HASH_MAPS_HEADERS)
add_library(ska_hash_maps INTERFACE)
target_include_directories(ska_hash_maps INTERFACE "${SKA_HASH_MAPS_HEADERS}")

add_subdirectory(libs)
add_subdirectory(compiler)