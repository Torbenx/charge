cmake_minimum_required(VERSION 3.18)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/parse/parse.cpp"
                          "${CMAKE_CURRENT_SOURCE_DIR}/parse/parse_gen.cpp"
                          "${CMAKE_CURRENT_SOURCE_DIR}/parse/parse_gen.h"
                   COMMAND ${Python3_EXECUTABLE}
                   ARGS "${CMAKE_CURRENT_SOURCE_DIR}/parse/parse.py"
                   DEPENDS parse/parse.txt parse/parse.py parse/parse.cpp.in)

add_executable(compiler
    log.cpp
    log.h
    PageBumpAllocator.h
    tests.cpp
    types.h
    windows_allocdetail.cpp
    WordTable.h
)

add_subdirectory(glue)
add_subdirectory(parse)
add_subdirectory(sema)

target_include_directories(compiler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(compiler PRIVATE "/W3;/wd4244;/wd4267")
else()
    target_compile_options(compiler PRIVATE "-Wall;-Wextra;-Wsuggest-override;-Wimplicit-fallthrough;-fsized-deallocation")
endif()
target_link_libraries(compiler PRIVATE fmt)
target_compile_definitions(compiler PRIVATE COMPILER_TEST_DIR="${CMAKE_SOURCE_DIR}/tests")