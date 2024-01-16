#pragma once

#include <fmt/format.h>

[[noreturn]] void handle_failure();
[[noreturn]] void verify_failed(const char* condStr, const char* file, const char* func, int line);
[[noreturn]] void verify_not_reached(const char* file, const char* func, int line);

constexpr void verify(bool cond, const char* condStr, const char* file, const char* func, int line) {
    if (!cond) [[unlikely]]
        verify_failed(condStr, file, func, line);
}
template<typename L, typename R>
constexpr void expect_eq(const L& lhs, const R& rhs, const char* lhsStr, const char* rhsStr, const char* file, const char* func, int line) {
    bool b = lhs != rhs;
    if (b) [[unlikely]] {
        fmt::println("EXPECT failed {}:{}: {}(): {} {{{}}} == {} {{{}}}", file, line, func, lhsStr, lhs, rhsStr, rhs);
        handle_failure();
    }
}

#define VERIFY(cond) ::verify(cond, #cond, __FILE__, __func__, __LINE__)
#define EXPECT_EQ(lhs, rhs) ::expect_eq(lhs, rhs, #lhs, #rhs, __FILE__, __func__, __LINE__)
#define VERIFY_NOT_REACHED() ::verify_not_reached(__FILE__, __func__, __LINE__);