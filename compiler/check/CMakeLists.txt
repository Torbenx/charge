cmake_minimum_required(VERSION 3.18)

target_sources(compiler
    PRIVATE
    BasicBlock.h
    BooleanEquality.h
    BooleanVariables.h
    LiteralInfo.h
    Reason.h
    SatSolver.h
    SatSolver.cpp
    SatTests.cpp
    StandardEquality.h
    StandardEquality.cpp
    TopologicalOrder.h
    Value.h
    ValueTheory.h
)