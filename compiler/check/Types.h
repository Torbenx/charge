#pragma once

#include <types.h>
#include <check/ValueTheory.h>
#include <check/PartialOrdering.h>
#include <check/LoadSet.h>

namespace check {

struct TypeLoadInfo {
    StandardEquality::EqualityInfo equalityInfo;
};

struct TypeLoads : TypeTheory, LoadSet<TypeLoads, TypeLoadInfo> {
    TypeLoads(Solver& solver)
        : TypeTheory(solver), baseLabel(solver, ValueCategory::Load) { }

    Value defineLoad(Solver& solver, MemoryLocation loc, CodePosition pos) {
        auto id = LoadSet::get(solver, loc, pos);
        return Value { (uint32_t)theoryId(), id };
    }

    void collectValueInactiveReasons(Solver& solver, Value v, std::vector<BooleanValue>& clause) override {
        collectLoadInactiveReasons(solver, v.valueId, clause);
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

    std::optional<ValueKind> scalarKind(Solver&, Type) override { return std::nullopt; }
    std::optional<Type> dereferencedType(Solver&, Type) override { return std::nullopt; }
    std::optional<Type> memberExpressionMemberType(Solver&, Type) override { return std::nullopt; }
    std::optional<Type> memberExpressionBaseType(Solver&, Type) override { return std::nullopt; }

private:
    friend LoadSet;

    TypeLoadInfo makeData(Solver&, uint32_t newId, MemoryLocation, CodePosition) {
        return {
            .equalityInfo = EqualityInfo({ (uint32_t)theoryId(), newId }),
        };
    }

    ValueBaseLabel baseLabel;
};

struct Types : ValueKindTheory {
    static PartialOrderingsSet possibleOrderings(Solver&, Type, Type) {
        return PartialOrderingsSet::all();
    }

    Types(Solver& solver)
    : m_loads(solver), m_ordering(solver) { }

    std::string formatValueKind(Solver&, ValueKind) override {
        return "type";
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
        using PartialOrderingTheory::PartialOrderingTheory;

        PartialOrderingsSet possibleOrderings(Solver& solver, Value a, Value b) override {
            return Types::possibleOrderings(solver, Type { a }, Type { b });
        }
    };

    TypeLoads m_loads;
    Ordering m_ordering;
};

/*struct ConcreteTypeHandle {
    uint32_t id;
};

struct TreeIndex {
    uint16_t forwardIndex;
    uint16_t backwardIndex;

    std::partial_ordering operator<=>(const TreeIndex& other) const {
        auto fo = forwardIndex <=> other.forwardIndex;
        auto bo = backwardIndex <=> other.backwardIndex;
        if (fo == bo)
            return fo;
        return std::partial_ordering::unordered;
    }
    bool operator==(const TreeIndex& other) const {
        return forwardIndex == other.forwardIndex && backwardIndex == other.backwardIndex;
    }
};

struct ConcreteType {

    struct ContainedType {
        ConcreteTypeHandle type;
        uint16_t instanceOffset;
        uint16_t instanceCount;
    };

    std::span<const ContainedType> memberTypes() const { return m_memberTypes; }
    std::span<const ContainedType> hasMemberTypes() const { return m_hasMemberTypes; }
    ConcreteTypeHandle typeAt(TreeIndex index) const { return forwardMembers[index.forwardIndex]; }
    std::span<const TreeIndex> instancesOf(ContainedType ct) const {
        return { typeInstances.data() + ct.instanceOffset, ct.instanceCount };
    }

    std::vector<ContainedType> m_memberTypes;
    std::vector<ContainedType> m_hasMemberTypes;
    std::vector<ConcreteTypeHandle> forwardMembers;
    std::vector<TreeIndex> typeInstances;
};

struct GenericType {
    std::vector<Type> m_directMemberTypes;
    std::vector<Type> m_directHasMemberTypes;
};*/

}