cmake_minimum_required(VERSION 3.18)

target_sources(compiler
    PRIVATE
    Context.h
    dump.cpp
    generate.cpp
    Generator.h
    InsertOperation.h
    Node.cpp
    Node.h
    parse.cpp
    Program.cpp
    Program.h
    Scope.h
    Value.h
)