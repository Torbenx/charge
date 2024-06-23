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
    virtual uint64_t labelOf(Solver&, Value) = 0;

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
    virtual void propagateFalseAssignment(Solver&, BooleanValue) = 0;

    //! Reapply an assignment that was reverted during ReasonTheory::backtrack()
    /*!
    Should only be used when a reason theory also uses backtrack.
    */
    virtual void reapplyFalseAssignment(Solver&, BooleanValue) = 0;

    //! Revert an assignment that was not reverted during ReasonTheory::backtrack()
    /*!
    Should not be used when a reason theory also reverts assignments in backtrack.
    */
    virtual void unapplyFalseAssignment(Solver&, BooleanValue) = 0;
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
    std::string formatValue(Solver& solver, Value v) override {
        auto lit = BooleanValue { v };
        int_t varId = variableId(lit);
        if (isPositive(lit))
            return formatPositiveLiteral(solver, varId);
        else
            return formatNegativeLiteral(solver, varId);
    }

    void enumerateValues(Solver&, std::function<void(Value)> f) override {
        for (int_t i = find; i < variableCount(); i++) {
            f(positiveLiteral(i));
            f(negativeLiteral(i));
        }
    }

    int_t newVariable() {
        int_t id = variableCount();
        infos.resize(infos.size() + 2);
        return id;
    }

    std::optional<int_t> findUnassignedVariable() {
        for (int_t i = find; i < variableCount(); i++) {
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

    int_t variableId(BooleanValue v) const { return v.valueId >> 1; }
    bool isPositive(BooleanValue v) const { return (v.valueId & 1u) == 0u; }

    int_t variableCount() const { return infos.size() >> 1; }

    BooleanValue positiveLiteral(int_t varId) const { return { (uint32_t)theoryId(), (uint32_t)varId * 2u }; }
    BooleanValue negativeLiteral(int_t varId) const { return { (uint32_t)theoryId(), (uint32_t)varId * 2u + 1u }; }

    uint64_t labelOf(Solver&, Value) override { VERIFY_NOT_REACHED(); }

private:
    std::vector<T> infos;
    int_t find = 0;
};

}