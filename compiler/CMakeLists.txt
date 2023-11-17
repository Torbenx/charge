cmake_minimum_required(VERSION 3.18)

add_executable(compiler
    Lexer.cpp
    Parser.cpp
    log.cpp
    tests.cpp
    syntax_dump.cpp
    semantic.cpp
    ir_dump.cpp
)
target_compile_options(compiler PRIVATE "-Wall;-Wextra;-Wsuggest-override;-Wimplicit-fallthrough;-fsized-deallocation")
target_link_libraries(compiler PRIVATE fmt)
target_compile_definitions(compiler PRIVATE COMPILER_TEST_DIR="${CMAKE_SOURCE_DIR}/test")