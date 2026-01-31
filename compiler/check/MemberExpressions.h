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

    MemberExpressionLoads(Solver& solver)
    : MemberExpressionTheory(solver), baseLabel(solver, ValueCategory::Load) { }

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
        return baseLabel + LoadSet::label(v.valueId);
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

    std::optional<LiteralInfo> literalInfo(Solver&, MemberExpression) override { return std::nullopt; }

private:
    friend LoadSet;

    MemberExpressionLoadInfo makeData(Solver& solver, uint32_t newId, MemoryLocation loc, CodePosition) {
        Type type = solver.typeAtLocation(loc);
        std::optional<Type> memberType = solver.theoryFor(type).memberExpressionMemberType(solver, type);
        return {
            .equalityInfo = EqualityInfo({ (uint32_t)theoryId(), newId }),
            .memberType = memberType.value(),
        };
    }

    ValueBaseLabel baseLabel;
};

struct MemberExpressionLiteralData : MemberExpressionTheory::LiteralInfo {
    MemberExpressionTheory::EqualityInfo equalityInfo;
};

struct MemberExpressionLiterals : MemberExpressionTheory, private FlatTreeSetDetail::Base<MemberExpressionLiterals, MemberExpressionLiteralData> {

    MemberExpressionLiterals(Solver& solver)
        : MemberExpressionTheory(solver), baseLabel(solver, ValueCategory::Literal) { }

    MemberExpression identity(Solver& solver, Type type) {
        uint32_t id = Base::get(solver, type);
        return { (uint32_t)theoryId(), id };
    }

    void collectValueInactiveReasons(Solver& solver, Value v, std::vector<BooleanValue>& clause) override {
        solver.collectInactiveReasons(at(v.valueId).baseType, clause);
    }

    bool isValueActive(Solver& solver, Value v) override {
        return solver.isActive(at(v.valueId).baseType);
    }

    uint64_t labelOfValue(Solver&, Value v) override {
        return baseLabel + (uint64_t)Base::label(v.valueId);
    }

    std::string formatValue(Solver& solver, Value v) override {
        return "self{" + solver.formatValue(at(v.valueId).baseType) + "}";
    }

    void enumerateValues(Solver&, std::function<void(Value)> f) override {
        for (int_t i = 0; i < Base::size(); i++)
            f(Value { (uint32_t)theoryId(), (uint32_t)i });
    }

    EqualityInfo& equalityInfo(Solver&, Value v) override {
        return at(v.valueId).equalityInfo;
    }

    Type memberType(Solver&, MemberExpression expr) override {
        return at(expr.valueId).baseType; // Note: Currently only the indentity literal is supported
    }

    std::optional<LiteralInfo> literalInfo(Solver&, MemberExpression expr) override {
        return at(expr.valueId);
    }

private:
    friend Base;

    std::strong_ordering compare(Solver& solver, Type a, const MemberExpressionLiteralData& data) {
        return solver.compare(a, data.baseType);
    }

    uint32_t makeNode(Solver&, Type baseType, TreeLabel label) {
        MemberExpression newExpr { (uint32_t)theoryId(), (uint32_t)Base::nextNodeHandle() };
        return Base::makeNode(label, { LiteralInfo { baseType }, EqualityInfo(newExpr) });
    }

    ValueBaseLabel baseLabel;
};

struct MemberExpressions : ValueKindTheory {
    static PartialOrderingsSet possibleOrderings(Solver& solver, MemberExpression a, MemberExpression b) {
        if (auto aLiteral = solver.literalInfo(a); aLiteral.has_value()) {
            if (auto bLiteral = solver.literalInfo(b); bLiteral.has_value()) {
                // Base types must be the same for this to even be considered, see isOrderingActive() override
                VERIFY(solver.assignedTrue(solver.equality(aLiteral->baseType, bLiteral->baseType)));
                // There currently only one possible member expression per type (the identity) so this is always equal.
                return PartialOrderingsSet::equal();
            }
        }
        return solver.possibleOrderings(solver.memberType(a), solver.memberType(b));
    }

    MemberExpressions(Solver& solver)
    : m_loads(solver), m_literals(solver), m_ordering(solver) { }

    MemberExpressionLiterals& literals() { return m_literals; }

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
        Ordering(Solver& solver)
        : PartialOrderingTheory(solver) { }

        PartialOrderingsSet possibleOrderings(Solver& solver, Value a, Value b) override {
            return MemberExpressions::possibleOrderings(solver, MemberExpression { a }, MemberExpression { b });
        }

        bool isOrderingActive(Solver& solver, Value va, Value vb) override {
            MemberExpression a { va }, b { vb };
            if (!PartialOrderingTheory::isOrderingActive(solver, a, b))
                return false;
            if (auto aLiteral = solver.literalInfo(a); aLiteral.has_value()) {
                if (auto bLiteral = solver.literalInfo(b); bLiteral.has_value()) {
                    if (!solver.assignedTrue(solver.equality(aLiteral->baseType, bLiteral->baseType)))
                        return false;
                }
            }
            return true;
        }

        void collectOrderingInactiveReasons(Solver& solver, Value va, Value vb, std::vector<BooleanValue>& clause) override {
            MemberExpression a { va }, b { vb };
            PartialOrderingTheory::collectOrderingInactiveReasons(solver, a, b, clause);
            if (auto aLiteral = solver.literalInfo(a); aLiteral.has_value()) {
                if (auto bLiteral = solver.literalInfo(b); bLiteral.has_value()) {
                    clause.push_back(solver.disequality(aLiteral->baseType, bLiteral->baseType));
                }
            }
        }
    };

    MemberExpressionLoads m_loads;
    MemberExpressionLiterals m_literals;
    Ordering m_ordering;
};

}