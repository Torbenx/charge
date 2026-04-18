cmake_minimum_required(VERSION 3.18)

target_sources(compiler
    PRIVATE
    IdentifierTable.h
    parse.cpp
    parse_impl.h
    parse_gen.cpp
    parse_gen.h
    recovery.cpp
    TokenBuffer.h
    Token.cpp
    Token.h
)