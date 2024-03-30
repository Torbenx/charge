cmake_minimum_required(VERSION 3.18)

target_sources(compiler
    PRIVATE
    InsertOperation.h
    generate.cpp
    Generator.h
    Node.h
    parse.cpp
    Program.cpp
    Program.h
)