
add_subdirectory(backend)

target_sources(compiler
    PRIVATE
    Expr.h
    ExpressionStorage.cpp
    ExpressionStorage.h
    Problem.h
    z3.h
    z3tests.cpp
)