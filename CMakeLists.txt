cmake_minimum_required(VERSION 3.18)
project(Charge_Language)

set(CHARGE_GNU_X86_VERSION "" CACHE STRING "x86-64 microarchitecture level; empty keeps the toolchain's flags")
set_property(CACHE CHARGE_GNU_X86_VERSION PROPERTY STRINGS "" v1 v2 v3 v4)

function(charge_apply_x86_version)
    if(CHARGE_GNU_X86_VERSION)
        set(flag "-march=x86-64-${CHARGE_GNU_X86_VERSION}")
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:${flag}>")
    endif()
endfunction()

if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
    if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(x86_64|x64)$")
        charge_apply_x86_version()
    endif()
else()
    # Before CMake 4.1 CMAKE_CXX_COMPILER_ARCHITECTURE_ID was not set for unix and apple
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
        charge_apply_x86_version()
    endif()
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

add_subdirectory(compiler)