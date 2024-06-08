#pragma once

#include <check/SatSolver.h>

namespace check::sat {

struct BoolTheory : Theory {
    int_t thisTheoryId = -1;
    std::vector<LiteralInfo> infos;
    int_t find = 0;

    std::string format(Literal lit) override {
        std::string result = std::to_string(lit.literalId >> 1);
        if ((lit.literalId & 1u) != 0)
            result.insert(result.begin(), '-');
        return result;
    }

    Literal negate(Literal lit) override {
        return { lit.theoryId, lit.literalId ^ 1u };
    }

    LiteralInfo* getInfo(Literal lit) override {
        return &infos[lit.literalId];
    }

    void assignFalse(Literal) override { }
    void reverseFalseAssignment(Literal) override { }

    void setTheoryId(int_t id) override {
        thisTheoryId = id;
    }

    int_t newVariable() {
        int_t id = infos.size() / 2;
        infos.resize(infos.size() + 2);
        return id;
    }

    void enumerateLiterals(std::function<void(Literal)> f) override {
        for (int_t i = find; i < (int_t)infos.size() / 2; i++) {
            f(positiveLiteral(i));
            f(negativeLiteral(i));
        }
    }

    std::optional<int_t> findUnassignedVariable() {
        for (int_t i = find; i < (int_t)infos.size() / 2; i++) {
            if (getInfo(positiveLiteral(i))->assignedFalse() || getInfo(negativeLiteral(i))->assignedFalse())
                continue;
            find = i;
            return i;
        }
        for (int_t i = 0; i < find; i++) {
            if (getInfo(positiveLiteral(i))->assignedFalse() || getInfo(negativeLiteral(i))->assignedFalse())
                continue;
            find = i;
            return i;
        }
        return std::nullopt;
    }

    Literal positiveLiteral(int_t varId) const { return { (uint32_t)thisTheoryId, (uint32_t)varId * 2u }; }
    Literal negativeLiteral(int_t varId) const { return { (uint32_t)thisTheoryId, (uint32_t)varId * 2u + 1u }; }
    Literal literalFromSign(int_t var) const { return var < 0 ? negativeLiteral(-var) : positiveLiteral(var); }
};

}