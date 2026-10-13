cmake_minimum_required(VERSION 3.18)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/parse/parse.cpp"
                          "${CMAKE_CURRENT_SOURCE_DIR}/parse/parse_gen.cpp"
                          "${CMAKE_CURRENT_SOURCE_DIR}/parse/parse_gen.h"
                          "${CMAKE_CURRENT_SOURCE_DIR}/parse/keyword_table.gperf"
                   COMMAND ${Python3_EXECUTABLE}
                   ARGS "${CMAKE_CURRENT_SOURCE_DIR}/parse/parse.py"
                   DEPENDS parse/parse.txt parse/parse.py parse/parse.cpp.in)

project(charge)

add_executable(${PROJECT_NAME}
    BigInt.cpp
    BigInt.h
    EnumTable.h
    FlatSet.cpp
    FlatSet.h
    FlatTreeSet.cpp
    FlatTreeSet.h
    IntOr.cpp
    IntOr.h
    log.cpp
    log.h
    main.cpp
    padded_string.cpp
    padded_string.h
    padded_string_compare.cpp
    padded_string_compare.h
    PageBumpAllocator.h
    ReverseMemberPointer.h
    tests.cpp
    types.cpp
    types.h
    WordStringTable.h
    WordTable.h
    WordTranslationTable.cpp
    WordTranslationTable.h
)

if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE windows_allocdetail.cpp)
else()
    target_sources(${PROJECT_NAME} PRIVATE linux_allocdetail.cpp)
endif()

add_subdirectory(parse)
add_subdirectory(sema)
add_subdirectory(server)
add_subdirectory(verify)

target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "/W3")
else()
    target_compile_options(${PROJECT_NAME} PRIVATE "-Wall;-Wextra;-Wsuggest-override;-Wimplicit-fallthrough;-Wno-logical-not-parentheses;-Wno-dangling-else")
    if(MINGW)
        target_link_libraries(${PROJECT_NAME} PRIVATE stdc++exp)
    endif()
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE gtest CLI11::CLI11)
target_compile_definitions(${PROJECT_NAME} PRIVATE COMPILER_TEST_DIR="${CMAKE_SOURCE_DIR}/tests")

set(CHARGE_BINARY "$<TARGET_FILE:${PROJECT_NAME}>")
set(CHIRAL_BINARY "$<TARGET_FILE_DIR:${PROJECT_NAME}>/chiral$<TARGET_FILE_SUFFIX:${PROJECT_NAME}>")
if(WIN32)
    # Creating symlinks on Windows requires developer mode or administrator rights
    set(CHIRAL_LINK_COMMAND "copy_if_different")
else()
    set(CHIRAL_LINK_COMMAND "create_symlink")
endif()
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" ARGS "-E" "${CHIRAL_LINK_COMMAND}" "${CHARGE_BINARY}" "${CHIRAL_BINARY}"
)