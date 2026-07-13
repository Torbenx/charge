cmake_minimum_required(VERSION 3.18)

target_sources(${PROJECT_NAME}
    PRIVATE
    Constant.h
    Context.cpp
    Context.h
    dump.cpp
    errors.h
    Formatter.cpp
    Formatter.h
    generate.cpp
    Generator.h
    Instruction.cpp
    Instruction.h
    parse.cpp
    Program.cpp
    Program.h
    Scope.h
    Util.cpp
    Util.h
)