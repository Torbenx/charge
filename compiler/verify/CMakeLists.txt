
add_subdirectory(backend)
add_subdirectory(ir)
add_subdirectory(language)

target_sources(${PROJECT_NAME}
    PRIVATE
    z3.h
    z3tests.cpp
)