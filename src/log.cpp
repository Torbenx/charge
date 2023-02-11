#include "log.h"

void verify_failed(const char* condStr, const char* file, const char* func, int line) {
    fmt::println("VERIFY failed {}:{}: {}(): {}", file, line, func, condStr);
    handle_failure();
}

void handle_failure() {
    std::abort();
}