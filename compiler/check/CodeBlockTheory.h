#pragma once

#include <check/Value.h>

namespace check {

struct Solver;

struct CodeBlockTheory {
    CodeBlockTheory(Solver&);
    virtual ~CodeBlockTheory() = default;
    CodeBlockTheory(const CodeBlockTheory&) = delete;
    CodeBlockTheory(CodeBlockTheory&&) = delete;
    CodeBlockTheory& operator=(const CodeBlockTheory&) = delete;
    CodeBlockTheory& operator=(CodeBlockTheory&&) = delete;

    virtual std::string formatBlockName(Solver&, BlockId) = 0;
    virtual std::string formatCodePosition(Solver&, CodePosition) = 0;

    virtual uint64_t labelOf(Solver&, BlockId) = 0;
    virtual Value loadAtEndOfBlock(Solver&, MemoryLocation, BlockId) = 0;
    virtual Value loadAtPosition(Solver&, MemoryLocation, CodePosition) = 0;

    int_t theoryId() const { return m_theoryId; }

private:
    uint8_t m_theoryId;
};

}