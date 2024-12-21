#pragma once

#include <check/StandardEquality.h>

namespace check {

/*struct MemoryLocations;

struct MemoryLocationEquality : StandardEquality {
    MemoryLocations* kindTheory();
    void onNewVariable(Solver&, int_t eqId) override;
    void watch(Solver&, Value, Value) override;
    bool isDisequalityWatched(Solver&, Value, Value) override;

    BooleanVariables glueVariables;
};

struct MemoryLocationLoadInfo {
    EquatableValueTheory::EqualityInfo equalityInfo;
    Type type;
};

struct MemoryLocationLoads : MemoryLocationTheory, private LoadSet<MemoryLocationLoads, MemoryLocationLoadInfo> {
    using MemoryLocationTheory::MemoryLocationTheory;

    Value defineLoad(Solver& solver, MemoryLocation loc, CodePosition pos) {
        auto id = LoadSet::get(solver, loc, pos);
        return Value { (uint32_t)theoryId(), id };
    }

    uint64_t labelOfValue(Solver&, Value v) override {
        return baseLabel + (uint64_t)LoadSet::label(v.valueId);
    }

    EqualityInfo& equalityInfo(Solver&, Value v) override {
        return LoadSet::at(v.valueId).equalityInfo;
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
    uint64_t baseLabel = 0;

    MemoryLocationLoadInfo makeData(Solver& solver, uint32_t newId, MemoryLocation loc, CodePosition) {
        Type type = solver.typeAtLocation(loc);
        std::optional<Type> pointeeType = solver.theoryFor(type).dereferencedType(solver, type);
        return { EqualityInfo({ (uint32_t)theoryId(), newId }), pointeeType.value() };
    }

    friend LoadSet;
};

struct DerivedMemoryLocations : MemoryLocationTheory {
    struct Info {
        EqualityInfo equalityInfo;
        MemoryLocation base;
        MemberExpression member;
    };

    Info& infoFor(MemoryLocation location) { return infos[location.valueId]; }

    uint64_t labelOfValue(Solver&, Value) override { VERIFY_NOT_REACHED(); }

    Type typeAtLocation(Solver& solver, MemoryLocation location) override {
        return solver.memberType(infoFor(location).member);
    }

    EqualityInfo& equalityInfo(Solver&, Value value) override {
        return infoFor({ value }).equalityInfo;
    }

    std::string formatValue(Solver& solver, Value value) override {
        auto& info = infoFor({ value });
        return solver.formatValue(info.base) + "." + solver.formatValue(info.member);
    }

    void enumerateValues(Solver&, std::function<void(Value)> f) override {
        for (int_t i = 0; i < (int_t)infos.size(); i++)
            f({ (uint32_t)theoryId(), (uint32_t)i });
    }

private:
    std::vector<Info> infos;
};

struct MemoryLocations : ValueKindTheory {
    struct NormalRepresentation {
        MemoryLocation base; //!< Either a load or base location
        std::span<const MemberExpression> expr; //!< Atomic member expressions
    };

    std::string formatValueKind(Solver&, ValueKind) override {
        return "memory_location";
    }

    BooleanValue equality(Solver&, Value a, Value b) override;

    BooleanValue disequality(Solver& solver, Value a, Value b) override {
        return solver.negate(equality(solver, a, b));
    }

    Value defineLoad(Solver& solver, MemoryLocation location, CodePosition position) override {
        return m_loads.defineLoad(solver, location, position);
    }

    NormalRepresentation normalRepresentation(Solver&, MemoryLocation);
    MemoryLocation fromNormalRepresentation(Solver&, NormalRepresentation);

    MemoryLocationEquality m_equality;
    MemoryLocationLoads m_loads;
    DerivedMemoryLocations m_derived;
};

struct ContainsRelation {
    Type parent;
    Type member;
};

struct TypeSet {
    TypeSet(std::initializer_list<Type>);
    TypeSet unionWith(const TypeSet& other);
    bool contains(Type type) const;
    bool insert(Type type);
    Type* begin();
    Type* end();
};

struct TypeInfo {
    std::vector<ContainsRelation> watches;
    TypeSet members;
};

struct TypeContainsRelation : SimpleBooleanTheory, ReasonTheory {

    TypeContainsRelation(Solver& solver)
        : SimpleBooleanTheory(solver), ReasonTheory(solver, false) { }

    static Reason makeReason(ContainsRelation relation);
    static ContainsRelation reasonRelation(const Reason& reason);

    void propagateFalseAssignment(Solver& solver, BooleanValue literal) override {
        if (isPositive(literal))
            return;

        auto [parent, member] = relation(variableId(literal));
        auto& parentInfo = infoFor(solver, parent);
        auto& memberInfo = infoFor(solver, member);

        auto updateNewMember = [this, &solver, &parentInfo](Type newMember) {
            auto& info = infoFor(solver, newMember);
            for (auto watch : parentInfo.watches) {
                if (watch.member == newMember) {
                    // !(A contains B) !(B contains C) (A contains C)
                    solver.assignTrue(positiveLiteral(relationId(solver, watch)), makeReason(watch));
                } else {
                    info.watches.push_back(watch);
                }
            }
        };

        if (parentInfo.members.insert(member)) {
            updateNewMember(member);

            auto newMembers = parentInfo.members.unionWith(memberInfo.members);
            for (Type newMember : newMembers)
                updateNewMember(newMember);

            positiveTrace.push_back({ { parent, member }, std::move(newMembers) });
        }
    }

    bool testReason(Solver& solver, const Reason& reason) override {
        auto [parent, member] = reasonRelation(reason);
        return infoFor(solver, parent).members.contains(member);
    }

    ClauseAndIndex reasonToClause(Solver& solver, const Reason& reason) override {
        auto [parent, member] = reasonRelation(reason);


    }

    int_t relationId(Solver&, ContainsRelation);
    static TypeInfo& infoFor(Solver&, Type);
    ContainsRelation relation(int_t relationId);

    struct PositiveTraceEntry {
        ContainsRelation relation;
        TypeSet newMembers;
    };

    std::vector<PositiveTraceEntry> positiveTrace;
};

struct Types : ValueKindTheory {
    StandardEquality m_equality;
};*/

}