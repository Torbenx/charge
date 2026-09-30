#pragma once

#include <WordStringTable.h>

namespace verify::language {

inline constexpr ConstWordStringTable words {
    "active",
    "from",
    "store",
    "call",
    "jump",
    "branch",
    "phi",
    "nop",
    "true",
    "false",
    "and",
    "or",
    "load",
    "pre",
    "post",
    "prove",
    "clause",
    "by",
    "union",
    "intersection",
    "setminus",
    "mset",
    "iset",
    "incl_iset",
    "excl_iset",
    "path_iset",
    "exact_iset",
    "invariant",
    "invariants",
    "struct",
#define TACTIC(name, snake_case) #snake_case,
#include <verify/ir/tactics.inc>
#define SORT(name, snake_case) #snake_case, #snake_case "_scalar",
#include <verify/ir/sorts.inc>
};

struct IdentifierTable : WordStringTable {
    IdentifierTable()
        : WordStringTable(words) { }
};

}