cmake_minimum_required(VERSION 3.18)

target_sources(compiler
    PRIVATE
    SatBoolTheory.h
    SatSolver.h
    SatSolver.cpp
    SatTests.cpp
)