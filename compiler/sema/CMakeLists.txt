cmake_minimum_required(VERSION 3.18)

target_sources(compiler
    PRIVATE
    dump.cpp
    InsertOperation.h
    generate.cpp
    Generator.h
    Node.cpp
    Node.h
    parse.cpp
    Program.cpp
    Program.h
    Value.h
)