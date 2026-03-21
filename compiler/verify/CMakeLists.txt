
add_subdirectory(backend)

target_sources(compiler
    PRIVATE
    Expr.h
    ExpressionStorage.cpp
    ExpressionStorage.h
    Problem.cpp
    Problem.h
    z3.h
    z3tests.cpp
)