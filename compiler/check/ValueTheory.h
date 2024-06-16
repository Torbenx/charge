#pragma once

#include <check/Value.h>

namespace check {

struct Solver;

struct ValueTheory {
    ValueTheory(Solver& solver);
    virtual ~ValueTheory() = default;
    ValueTheory(const ValueTheory&) = delete;
    ValueTheory(ValueTheory&&) = delete;
    ValueTheory& operator=(const ValueTheory&) = delete;
    ValueTheory& operator=(ValueTheory&&) = delete;

    virtual Type typeOf(Solver&, Value) = 0;

    virtual std::string formatValue(Solver&, Value) = 0;

    virtual void enumerateValues(Solver&, std::function<void(Value)> visitor) = 0;

    int_t theoryId() const { return m_theoryId; }

private:
    int_t m_theoryId;
};

struct BooleanTheory : ValueTheory {
    struct LiteralInfo;

    using ValueTheory::ValueTheory;

    Type typeOf(Solver&, Value) override { return builtins::boolean_type; }

    virtual BooleanValue negate(Solver&, BooleanValue) = 0;
    virtual LiteralInfo& literalInfo(Solver&, BooleanValue) = 0;
    virtual void assignFalse(Solver&, BooleanValue) = 0;
    virtual void revertFalseAssignment(Solver&, BooleanValue) = 0;
};

struct TypeTheory : ValueTheory {
    using ValueTheory::ValueTheory;
};

template<std::derived_from<BooleanTheory::LiteralInfo> T = BooleanTheory::LiteralInfo>
struct SimpleBooleanTheory : BooleanTheory {
    using BooleanTheory::BooleanTheory;

    BooleanValue negate(Solver&, BooleanValue lit) override {
        return negate(lit);
    }
    BooleanValue negate(BooleanValue lit) {
        return { lit.theoryId, lit.valueId ^ 1u };
    }
    LiteralInfo& literalInfo(Solver&, BooleanValue lit) override {
        return literalInfo(lit);
    }
    LiteralInfo& literalInfo(BooleanValue lit) {
        return infos[lit.valueId];
    }

    virtual std::string formatPositiveLiteral(Solver&, int_t varId) = 0;
    virtual std::string formatNegativeLiteral(Solver&, int_t varId) = 0;
    std::string formatValue(Solver& solver, Value lit) override {
        int_t varId = lit.valueId >> 1;
        if ((lit.valueId & 1u) == 0u)
            return formatPositiveLiteral(solver, varId);
        else
            return formatNegativeLiteral(solver, varId);
    }

    void enumerateValues(Solver&, std::function<void(Value)> f) override {
        for (int_t i = find; i < (int_t)infos.size() / 2; i++) {
            f(positiveLiteral(i));
            f(negativeLiteral(i));
        }
    }

    int_t newVariable() {
        int_t id = infos.size() / 2;
        infos.resize(infos.size() + 2);
        return id;
    }

    std::optional<int_t> findUnassignedVariable() {
        for (int_t i = find; i < (int_t)infos.size() / 2; i++) {
            if (literalInfo(positiveLiteral(i)).assignedFalse() || literalInfo(negativeLiteral(i)).assignedFalse())
                continue;
            find = i;
            return i;
        }
        for (int_t i = 0; i < find; i++) {
            if (literalInfo(positiveLiteral(i)).assignedFalse() || literalInfo(negativeLiteral(i)).assignedFalse())
                continue;
            find = i;
            return i;
        }
        return std::nullopt;
    }

    BooleanValue positiveLiteral(int_t varId) const { return { (uint32_t)theoryId(), (uint32_t)varId * 2u }; }
    BooleanValue negativeLiteral(int_t varId) const { return { (uint32_t)theoryId(), (uint32_t)varId * 2u + 1u }; }

private:
    std::vector<T> infos;
    int_t find = 0;
};

}