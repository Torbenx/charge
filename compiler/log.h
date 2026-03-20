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
[[noreturn]] void _verify_failed(const char* condStr, const char* file, const char* func, int line);
[[noreturn]] void _verify_not_reached(const char* file, const char* func, int line);

constexpr void _verify(bool cond, const char* condStr, const char* file, const char* func, int line) {
    if (!cond) [[unlikely]]
        _verify_failed(condStr, file, func, line);
}

#define VERIFY(cond) ::_verify(cond, #cond, __FILE__, __func__, __LINE__)
#define VERIFY_NOT_REACHED() ::_verify_not_reached(__FILE__, __func__, __LINE__);