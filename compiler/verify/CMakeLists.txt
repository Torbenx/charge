
add_subdirectory(backend)
add_subdirectory(ir)

target_sources(compiler
    PRIVATE
    z3.h
    z3tests.cpp
)