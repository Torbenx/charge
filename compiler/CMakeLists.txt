cmake_minimum_required(VERSION 3.18)

find_package (Python3 COMPONENTS Interpreter REQUIRED)

add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/parse.cpp"
                          "${CMAKE_CURRENT_SOURCE_DIR}/parse_gen.cpp"
                          "${CMAKE_CURRENT_SOURCE_DIR}/parse_gen.h"
                   COMMAND ${Python3_EXECUTABLE}
                   ARGS "${CMAKE_CURRENT_SOURCE_DIR}/parse.py"
                   DEPENDS parse.txt parse.py parse.cpp.in)

add_executable(compiler
    log.cpp
    log.h
    parse.cpp
    parse.h
    parse_gen.cpp
    parse_gen.h
    tests.cpp
    types.h
    WordTable.h
)
target_compile_options(compiler PRIVATE "-Wall;-Wextra;-Wsuggest-override;-Wimplicit-fallthrough;-fsized-deallocation")
target_link_libraries(compiler PRIVATE fmt)
target_compile_definitions(compiler PRIVATE COMPILER_TEST_DIR="${CMAKE_SOURCE_DIR}/tests")