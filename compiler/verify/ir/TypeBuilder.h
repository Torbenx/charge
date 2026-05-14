#pragma once

#include <verify/ir/Database.h>

namespace verify::ir {

struct TypeBuilder {
    Database& database() { return m_db; }



private:
    struct Member {
        Type type;
    };

    struct Impl {
        std::vector<Member> members;
        DContext dctx;

    };

    Database& m_db;
};

}