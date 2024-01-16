#include "log.h"

void verify_failed(const char* condStr, const char* file, const char* func, int line) {
    fmt::println("VERIFY failed {}:{}: {}(): {}", file, line, func, condStr);
    handle_failure();
}

void handle_failure() {
    throw std::runtime_error("");
}

void verify_not_reached(const char* file, const char* func, int line) {
    fmt::println("SHOULD NOT BE REACHED {}:{}: {}()", file, line, func);
    handle_failure();
}