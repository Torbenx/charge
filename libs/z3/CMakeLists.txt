
add_library(z3i INTERFACE)

target_include_directories(z3i INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

if (MINGW)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .dll ${CMAKE_FIND_LIBRARY_SUFFIXES})
endif()
find_library(z3lib z3 REQUIRED)
target_link_libraries(z3i INTERFACE "${z3lib}")