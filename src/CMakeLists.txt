cmake_minimum_required(VERSION 3.18)

add_executable(test
    main.cpp
    log.cpp
    Lexer.cpp
    Parser.cpp
)
target_compile_options(test PRIVATE "-Wall;-Wextra;-Wno-invalid-offsetof;-Wsuggest-override")
target_link_libraries(test PRIVATE fmt)