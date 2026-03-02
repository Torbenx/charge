#pragma once

#include <ReverseMemberPointer.h>
#include <check/PartialOrdering.h>
#include <check/StandardEquality.h>

namespace check {

struct MemoryLocationLoadInfo {
    Type typeAtLocation;
};

struct MemoryLocationLoads : MemoryLocationTheory, LoadSet<MemoryLocationLoads, MemoryLocationLoadInfo> {
    MemoryLocationLoads(Solver& solver)
        : MemoryLocationTheory(solver)
        , baseLabel(solver, ValueCategory::Load)
        , declarations(solver)
        , memberExpressions(solver) { }

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

    std::optional<Load> loadInfo(Solver&, Value v) override {
        return LoadSet::loadAt(v.valueId);
    }

    void collectValueInactiveReasons(Solver& solver, Value v, std::vector<BooleanValue>& clause) override {
        collectLoadInactiveReasons(solver, v.valueId, clause);
    }

    bool isValueActive(Solver& solver, Value v) override {
        return isLoadActive(solver, v.valueId);
    }

    uint64_t labelOfValue(Solver&, Value v) override {
        return baseLabel + (uint64_t)LoadSet::label(v.valueId);
    }

    Type typeAtLocation(Solver&, MemoryLocation loc) override {
        return LoadSet::at(loc.valueId).typeAtLocation;
    }

    std::string formatValue(Solver& solver, Value v) override {
        auto [loc, pos] = LoadSet::loadAt(v.valueId);
        return solver.formatLoad(loc, pos);
    }

private:
    friend LoadSet;

    MemoryLocationLoadInfo makeData(Solver& solver, [[maybe_unused]] uint32_t newId, MemoryLocation loc, CodePosition) {
        Type type = solver.typeAtLocation(loc);
        std::optional<Type> pointeeType = solver.theoryFor(type).dereferencedType(solver, type);
        return { .typeAtLocation = pointeeType.value() };
    }

    struct Declarations : MemoryDeclarationTheory {
        Declarations(Solver& solver)
            : MemoryDeclarationTheory(solver), baseLabel(solver, ValueCategory::Load) { }

        MemoryLocationLoads* theory() {
            return ReverseMemberPointer<&MemoryLocationLoads::declarations>::reverse(this);
        }
        LoadSet* loads() { return theory(); }

        uint64_t labelOfValue(Solver&, Value value) override {
            return baseLabel + (uint64_t)loads()->label(value.valueId);
        }
        std::string formatValue(Solver& solver, Value value) override {
            auto [loc, pos] = loads()->loadAt(value.valueId);
            return "declaration(" + solver.formatLoad(loc, pos) + ")";
        }

        std::optional<DeclarationInfo> declarationInfo(Solver&, MemoryDeclaration) override { return std::nullopt; }

        ValueBaseLabel baseLabel;
    };

    struct MemberExpressions : MemberExpressionTheory {
        MemberExpressions(Solver& solver)
            : MemberExpressionTheory(solver), baseLabel(solver, ValueCategory::Load) { }

        MemoryLocationLoads* theory() {
            return ReverseMemberPointer<&MemoryLocationLoads::memberExpressions>::reverse(this);
        }
        LoadSet* loads() { return theory(); }

        uint64_t labelOfValue(Solver&, Value value) override {
            return baseLabel + (uint64_t)loads()->label(value.valueId);
        }
        std::string formatValue(Solver& solver, Value value) override {
            auto [loc, pos] = loads()->loadAt(value.valueId);
            return "memberexpr(" + solver.formatLoad(loc, pos) + ")";
        }

        Type memberType(Solver&, MemberExpression value) override {
            return loads()->at(value.valueId).typeAtLocation;
        }

        std::optional<LiteralInfo> literalInfo(Solver&, MemberExpression) override { return std::nullopt; }

        ValueBaseLabel baseLabel;
    };

    ValueBaseLabel baseLabel;
    Declarations declarations;
    MemberExpressions memberExpressions;
};

struct MemoryLocations : ValueKindTheory {

    MemoryLocations(Solver& solver)
        : ValueKindTheory(solver, ValueKind::MemoryLocation), m_loads(solver), m_ordering(solver, ValueKind::MemoryLocation) { }

    static PartialOrderingsSet possibleOrderings(Solver& solver, MemoryLocation a, MemoryLocation b) {
        MemoryLocationTheory& ta = solver.theoryFor(a);
        MemoryLocationTheory& tb = solver.theoryFor(b);
        auto aDecl = solver.declarationInfo(ta.memoryDeclaration(solver, a));
        auto bDecl = solver.declarationInfo(tb.memoryDeclaration(solver, b));
        auto aLoad = ta.loadInfo(solver, a);
        auto bLoad = tb.loadInfo(solver, b);
        // 2 distinct declarations are always distinct
        if (aDecl.has_value() && bDecl.has_value())
            return PartialOrderingsSet::unordered();
        // Loaction cannot be loaded before its declared
        if (aDecl.has_value() && bLoad.has_value() && solver.compare(bLoad->position, aDecl->position) < 0)
            return PartialOrderingsSet::unordered();
        if (bDecl.has_value() && aLoad.has_value() && solver.compare(aLoad->position, bDecl->position) < 0)
            return PartialOrderingsSet::unordered();

        // This automatically consideres the location type since it is the same as the member type.
        return solver.possibleOrderings(ta.memberExpression(solver, a), tb.memberExpression(solver, b));
    }

    std::string formatValueKind(Solver&, ValueKind) override {
        return "memory-location";
    }

    BooleanValue equality(Solver& solver, Value a, Value b) override {
        return m_ordering.equality(solver, a, b);
    }

    Value defineLoad(Solver& solver, MemoryLocation location, CodePosition position) override {
        return m_loads.defineLoad(solver, location, position);
    }

private:
    struct Ordering : PartialOrderingTheory {
        using PartialOrderingTheory::PartialOrderingTheory;

        PartialOrderingsSet possibleOrderings(Solver& solver, Value a, Value b) override {
            return MemoryLocations::possibleOrderings(solver, MemoryLocation { a }, MemoryLocation { b });
        }
    };

    MemoryLocationLoads m_loads;
    Ordering m_ordering;
};

}