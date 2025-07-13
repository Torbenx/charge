cmake_minimum_required(VERSION 3.18)

target_sources(compiler
    PRIVATE
    Constant.h
    Context.cpp
    Context.h
    dump.cpp
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
    IdentifierTable.h
)