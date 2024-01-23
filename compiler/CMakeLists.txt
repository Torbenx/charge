cmake_minimum_required(VERSION 3.18)

find_package (Python3 COMPONENTS Interpreter REQUIRED)

get_filename_component(path_to_parse_py parse.py ABSOLUTE)
add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/parse.cpp"
                   COMMAND ${Python3_EXECUTABLE} ARGS ${path_to_parse_py}
                   DEPENDS parse.py parse.cpp.in)

add_executable(compiler
    log.cpp
    log.h
    nodes.h
    parse.cpp
    tests.cpp
    types.h
    WordTable.h
)
target_compile_options(compiler PRIVATE "-Wall;-Wextra;-Wsuggest-override;-Wimplicit-fallthrough;-fsized-deallocation")
target_link_libraries(compiler PRIVATE fmt)
target_compile_definitions(compiler PRIVATE COMPILER_TEST_DIR="${CMAKE_SOURCE_DIR}/tests")