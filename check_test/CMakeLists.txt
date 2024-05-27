
add_executable(check_test
    check.h
    log.cpp
    main.cpp
)

target_include_directories(check_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(check_test PRIVATE "/W3;/wd4244;/wd4267")
else()
    target_compile_options(check_test PRIVATE "-Wall;-Wextra;-Wsuggest-override;-Wimplicit-fallthrough;-fsized-deallocation")
endif()
target_link_libraries(check_test PRIVATE fmt)
target_compile_definitions(check_test PRIVATE CHECK_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")