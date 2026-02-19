#pragma once

#include <fmt/format.h>

template<typename... Args>
void println(fmt::format_string<Args...> str, Args&&... args) {
    fmt::println(stderr, str, std::forward<Args>(args)...);
}
template<typename... Args>
void print(fmt::format_string<Args...> str, Args&&... args) {
    fmt::print(stderr, str, std::forward<Args>(args)...);
}

[[noreturn]] void handle_failure();
[[noreturn]] void verify_failed(const char* condStr, const char* file, const char* func, int line);
[[noreturn]] void verify_not_reached(const char* file, const char* func, int line);

constexpr void verify(bool cond, const char* condStr, const char* file, const char* func, int line) {
    if (!cond) [[unlikely]]
        verify_failed(condStr, file, func, line);
}

#define VERIFY(cond) ::verify(cond, #cond, __FILE__, __func__, __LINE__)
#define VERIFY_NOT_REACHED() ::verify_not_reached(__FILE__, __func__, __LINE__);