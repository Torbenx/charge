cmake_minimum_required(VERSION 3.18)

target_sources(compiler
    PRIVATE
    api.cpp
    api.h
    IdentifierTable.h
    parse.cpp
    Parser.cpp
    Parser.h
    parse_gen.cpp
    parse_gen.h
    recovery.cpp
    TokenBuffer.h
    Token.cpp
    Token.h
)