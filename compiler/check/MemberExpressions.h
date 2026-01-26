#pragma once

#include <check/PartialOrdering.h>
#include <check/SatSolver.h>
#include <check/ValueTheory.h>

namespace check {

struct MemberExpressionLoadInfo {
    StandardEquality::EqualityInfo equalityInfo;
    Type memberType;
};

struct MemberExpressionLoads : MemberExpressionTheory, LoadSet<MemberExpressionLoads, MemberExpressionLoadInfo> {

    Value defineLoad(Solver& solver, MemoryLocation loc, CodePosition pos) {
        auto id = LoadSet::get(solver, loc, pos);
        return Value { (uint32_t)theoryId(), id };
    }

    void collectValueInactiveReasons(Solver& solver, Value v, std::vector<BooleanValue>& clause) override {
        return collectLoadInactiveReasons(solver, v.valueId, clause);
    }

    bool isValueActive(Solver& solver, Value v) override {
       return isLoadActive(solver, v.valueId);
    }

    uint64_t labelOfValue(Solver&, Value v) override {
        return baseLabel + (uint64_t)LoadSet::label(v.valueId);
    }

    std::string formatValue(Solver& solver, Value v) override {
        auto [loc, pos] = LoadSet::loadAt(v.valueId);
        return solver.formatLoad(loc, pos);
    }

    void enumerateValues(Solver&, std::function<void(Value)> f) override {
        for (int_t i = 0; i < LoadSet::size(); i++)
            f(Value { (uint32_t)theoryId(), (uint32_t)i });
    }

    EqualityInfo& equalityInfo(Solver&, Value v) override {
        return LoadSet::at(v.valueId).equalityInfo;
    }

    Type memberType(Solver&, MemberExpression expr) override {
        return LoadSet::at(expr.valueId).memberType;
    }

private:
    MemberExpressionLoadInfo makeData(Solver& solver, uint32_t newId, MemoryLocation loc, CodePosition) {
        Type type = solver.typeAtLocation(loc);
        std::optional<Type> memberType = solver.theoryFor(type).memberExpressionMemberType(solver, type);
        return {
            .equalityInfo = EqualityInfo({ (uint32_t)theoryId(), newId }),
            .memberType = memberType.value(),
        };
    }

    uint64_t baseLabel = 0;
};

struct MemberExpressions : ValueKindTheory {
    static PartialOrderingsSet possibleOrderings(Solver& solver, MemberExpression a, MemberExpression b) {
        return solver.possibleOrderings(solver.memberType(a), solver.memberType(b));
    }

    std::string formatValueKind(Solver&, ValueKind) override {
        return "member-expression";
    }

    BooleanValue equality(Solver& solver, Value a, Value b) override {
        return m_ordering.equality(solver, a, b);
    }

    BooleanValue disequality(Solver& solver, Value a, Value b) override {
        return solver.negate(equality(solver, a, b));
    }

    Value defineLoad(Solver& solver, MemoryLocation location, CodePosition position) override {
        return m_loads.defineLoad(solver, location, position);
    }

private:
    struct Ordering : PartialOrderingTheory {
        PartialOrderingsSet possibleOrderings(Solver& solver, Value a, Value b) override {
            return MemberExpressions::possibleOrderings(solver, MemberExpression { a }, MemberExpression { b });
        }
    };

    MemberExpressionLoads m_loads;
    Ordering m_ordering;
};

}