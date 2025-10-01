#pragma once

#include <ReverseMemberPointer.h>
#include <check/PartialOrdering.h>
#include <check/StandardEquality.h>

namespace check {

struct MemoryLocationLoadInfo {
    StandardEquality::EqualityInfo declarationEqualityInfo;
    StandardEquality::EqualityInfo memberExpressionEqualityInfo;
    Type type;
};

struct MemoryLocationLoads : MemoryLocationTheory, LoadSet<MemoryLocationLoads, MemoryLocationLoadInfo> {
    MemoryDeclaration memoryDeclaration(Solver&, MemoryLocation value) override {
        return { (uint32_t)declarations.theoryId(), value.valueId };
    }

    MemberExpression memberExpression(Solver&, MemoryLocation value) override {
        return { (uint32_t)memberExpressions.theoryId(), value.valueId };
    }

    Value defineLoad(Solver& solver, MemoryLocation loc, CodePosition pos) {
        auto id = LoadSet::get(solver, loc, pos);
        return Value { (uint32_t)theoryId(), id };
    }

    uint64_t labelOfValue(Solver&, Value v) override {
        return baseLabel + (uint64_t)LoadSet::label(v.valueId);
    }

    Type typeAtLocation(Solver&, MemoryLocation loc) override {
        return LoadSet::at(loc.valueId).type;
    }

    std::string formatValue(Solver& solver, Value v) override {
        auto [loc, pos] = LoadSet::loadAt(v.valueId);
        return solver.formatLoad(loc, pos);
    }

    void enumerateValues(Solver&, std::function<void(Value)> f) override {
        for (int_t i = 0; i < LoadSet::size(); i++)
            f(Value { (uint32_t)theoryId(), (uint32_t)i });
    }

private:
    using EqualityInfo = StandardEquality::EqualityInfo;

    MemoryLocationLoadInfo makeData(Solver& solver, uint32_t newId, MemoryLocation loc, CodePosition) {
        Type type = solver.typeAtLocation(loc);
        std::optional<Type> pointeeType = solver.theoryFor(type).dereferencedType(solver, type);
        return {
            .declarationEqualityInfo = EqualityInfo({ (uint32_t)declarations.theoryId(), newId }),
            .memberExpressionEqualityInfo = EqualityInfo({ (uint32_t)memberExpressions.theoryId(), newId }),
            .type = pointeeType.value(),
        };
    }

    struct Declarations : MemoryDeclarationTheory {
        MemoryLocationLoads* theory() {
            return ReverseMemberPointer<&MemoryLocationLoads::declarations>::reverse(this);
        }
        LoadSet* loads() { return theory(); }

        uint64_t labelOfValue(Solver& solver, Value value) {
            return baseLabel + (uint64_t)loads()->label(value.valueId);
        }
        std::string formatValue(Solver& solver, Value value) {
            auto [loc, pos] = loads()->loadAt(value.valueId);
            return "declaration(" + solver.formatLoad(loc, pos) + ")";
        }

        void enumerateValues(Solver&, std::function<void(Value)> f) {
            for (int_t i = 0; i < loads()->size(); i++)
                f(Value { (uint32_t)theoryId(), (uint32_t)i });
        }

        EqualityInfo& equalityInfo(Solver&, Value value) override {
            return loads()->at(value.valueId).declarationEqualityInfo;
        }

        std::optional<DeclarationInfo> declarationInfo(Solver&, MemoryDeclaration) override { return std::nullopt; }

        uint64_t baseLabel = 0;
    };

    struct MemberExpressions : MemberExpressionTheory {
        MemoryLocationLoads* theory() {
            return ReverseMemberPointer<&MemoryLocationLoads::memberExpressions>::reverse(this);
        }
        LoadSet* loads() { return theory(); }

        uint64_t labelOfValue(Solver& solver, Value value) {
            return baseLabel + (uint64_t)loads()->label(value.valueId);
        }
        std::string formatValue(Solver& solver, Value value) {
            auto [loc, pos] = loads()->loadAt(value.valueId);
            return "memberexpr(" + solver.formatLoad(loc, pos) + ")";
        }

        void enumerateValues(Solver&, std::function<void(Value)> f) {
            for (int_t i = 0; i < loads()->size(); i++)
                f(Value { (uint32_t)theoryId(), (uint32_t)i });
        }

        EqualityInfo& equalityInfo(Solver&, Value value) override {
            return loads()->at(value.valueId).memberExpressionEqualityInfo;
        }

        Type memberType(Solver&, MemberExpression value) override {
            return loads()->at(value.valueId).type;
        }

        uint64_t baseLabel = 0;
    };

    uint64_t baseLabel = 0;
    Declarations declarations;
    MemberExpressions memberExpressions;
};

struct MemoryLocations : ValueKindTheory {

    static PartialOrderingsSet possibleOrderings(Solver& solver, MemoryLocation a, MemoryLocation b) {
        MemoryLocationTheory& ta = solver.theoryFor(a);
        MemoryLocationTheory& tb = solver.theoryFor(b);
        if (solver.declarationInfo(ta.memoryDeclaration(solver, a)).has_value() && solver.declarationInfo(tb.memoryDeclaration(solver, b)).has_value()) {
            return PartialOrderingsSet::unordered();
        }

        // The location type is considered since it is the same as the member type.
        return solver.possibleOrderings(ta.memberExpression(solver, a), tb.memberExpression(solver, b));
    }

    std::string formatValueKind(Solver&, ValueKind) override {
        return "memory-location";
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
            return MemoryLocations::possibleOrderings(solver, MemoryLocation { a }, MemoryLocation { b });
        }
    };

    MemoryLocationLoads m_loads;
    Ordering m_ordering;
};

}