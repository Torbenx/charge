#include <log.h>

void _verify_failed(const char* condStr, const char* file, const char* func, int line) {
    println("VERIFY failed {}:{}: {}(): {}", file, line, func, condStr);
    handle_failure();
}

void handle_failure() {
    throw std::runtime_error("");
}

void _verify_not_reached(const char* file, const char* func, int line) {
    println("SHOULD NOT BE REACHED {}:{}: {}()", file, line, func);
    handle_failure();
}