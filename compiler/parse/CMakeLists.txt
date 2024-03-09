cmake_minimum_required(VERSION 3.18)

target_sources(compiler
    PRIVATE
    parse.cpp
    parse_impl.h
    parse_gen.cpp
    parse_gen.h
    Output.h
)