#pragma once

#include <ReverseMemberPointer.h>
#include <check/StandardEquality.h>
#include <check/Sets.h>

#include <bitset>
#include <compare>

namespace check {

static_assert(std::bit_cast<std::partial_ordering>(int8_t { -1 }) == std::partial_ordering::less);
static_assert(std::bit_cast<std::partial_ordering>(int8_t { 0 }) == std::partial_ordering::equivalent);
static_assert(std::bit_cast<std::partial_ordering>(int8_t { 1 }) == std::partial_ordering::greater);
static_assert(std::bit_cast<std::partial_ordering>(int8_t { 2 }) == std::partial_ordering::unordered);

constexpr int_t poToIndex(std::partial_ordering ordering) { return std::bit_cast<int8_t>(ordering) + 1; }
constexpr std::partial_ordering poFromIndex(int_t index) {
    VERIFY(index >= 0 && index < 4);
    return std::bit_cast<std::partial_ordering>(int8_t(index));
}

struct PartialOrderingsSet {

    static consteval PartialOrderingsSet all() { return PartialOrderingsSet(true); }
    static consteval PartialOrderingsSet none() { return PartialOrderingsSet(false); }
    static consteval PartialOrderingsSet less() { return { std::partial_ordering::less }; }
    static consteval PartialOrderingsSet less_equal() { return { std::partial_ordering::less, std::partial_ordering::equivalent }; }
    static consteval PartialOrderingsSet equal() { return { std::partial_ordering::equivalent }; }
    static consteval PartialOrderingsSet greater_equal() { return { std::partial_ordering::equivalent, std::partial_ordering::greater }; }
    static consteval PartialOrderingsSet greater() { return { std::partial_ordering::greater }; }
    static consteval PartialOrderingsSet unordered() { return { std::partial_ordering::unordered }; }

    constexpr PartialOrderingsSet(std::initializer_list<std::partial_ordering> orderings) {
        for (auto ordering : orderings)
            set(ordering, true);
    }

    constexpr void set(std::partial_ordering ordering, bool value = true) {
        m_set.set(poToIndex(ordering), value);
    }
    constexpr void clear() { m_set.reset(); }
    constexpr int_t count() const { return m_set.count(); }
    constexpr bool test(std::partial_ordering ordering) const {
        return m_set[poToIndex(ordering)];
    }

private:
    explicit constexpr PartialOrderingsSet(bool valueForAll) {
        m_set.set(0, valueForAll);
        m_set.set(1, valueForAll);
        m_set.set(2, valueForAll);
        m_set.set(3, valueForAll);
    }

    std::bitset<4> m_set;
};

struct PartialOrderingTheory {
    using Link = StandardEquality::Link;

    struct InternalHandle {
        uint32_t m_id;

        uint32_t id() const { return m_id; }
    };

    struct OrderingHandle {
        uint32_t m_id : 31;
        uint32_t m_flipped : 1;

        explicit OrderingHandle(InternalHandle handle, bool flipped = false)
            : m_id{ handle.id() }, m_flipped(flipped ? 1 : 0) { }

        uint32_t id() const { return m_id; }
        bool flipped() const { return m_flipped != 0; }
        InternalHandle internalHandle() const { return { id() }; }
    };

    PartialOrderingTheory(Solver& solver, uint64_t baseLabel);

    OrderingHandle order(Solver&, Value a, Value b);
    BooleanValue literal(Solver&, OrderingHandle, std::partial_ordering);

protected:
    PartialOrderingsSet possibleOrderings(Solver&, Value, Value) { return PartialOrderingsSet::all(); }

private:
    struct Entry {
        std::array<std::optional<BooleanValue>, 4> literals = {};
        SetTheory::SetFlags flags;
        Link link;

        Entry(Link link)
            : link(link) { }

        explicit operator OrientedPair() const { return link; }
    };

    struct Unordered : SimpleBooleanTheory {
        using SimpleBooleanTheory::SimpleBooleanTheory;

        PartialOrderingTheory* theory();

        BooleanValue newLiteral(Solver&, InternalHandle);
        std::string formatPositiveLiteral(Solver&, int_t varId) override;
        std::string formatNegativeLiteral(Solver&, int_t varId) override;
        void propagateAssignment(Solver&, BooleanValue) override;
        void unapplyAssignment(Solver&, BooleanValue) override;
        void reapplyAssignment(Solver&, BooleanValue) override;
        uint32_t labelOfVariable(Solver&, int_t varId) override;
        bool isVariableActive(Solver&, int_t varId) override;
        void collectVariableInactiveReasons(Solver&, int_t varId, std::vector<BooleanValue>& clause) override;

        std::vector<InternalHandle> m_handles;
    };

    struct Equality : StandardEquality {
        using StandardEquality::StandardEquality;

        PartialOrderingTheory* theory();

        BooleanValue newLiteral(Solver&, InternalHandle);
        bool isUnitDisequal(Solver& solver, Value a, Value b) override;
        Link equalityLink(int_t eqId) override;
        int_t lookupEqualityVariable(Solver&, Value, Value) override;
        uint32_t labelOfVariable(Solver&, int_t varId) override;

        std::vector<InternalHandle> m_handles;
    };

    struct OrderingSets : SetTheory {
        using SetTheory::SetTheory;

        PartialOrderingTheory* theory();

        SetFlags& setFlags(int_t setId) override;
        SetElements setElements(int_t setId) override;
        BooleanValue makeElement(Solver&, int_t setId, int_t index) override;

        // Make these public so they can be accessed by the theory
        using SetTheory::unitDeactivateElement;
        using SetTheory::propagateAssignment;
        using SetTheory::unapplyAssignment;

        BooleanValue getOrCreateOrderingLiteral(Solver&, OrderingHandle, std::partial_ordering);
    };

    void propagateAssignment(Solver&, InternalHandle, std::partial_ordering, bool);
    void unapplyAssignment(Solver&, InternalHandle, std::partial_ordering, bool);
    void reapplyAssignment(Solver&, InternalHandle, std::partial_ordering, bool);

    bool isOrderingActive(Solver&, InternalHandle);
    void collectInactiveReasons(Solver&, InternalHandle, std::vector<BooleanValue>& clause);

    Entry& entryAt(InternalHandle handle) { return m_entries.at(handle.id()); }
    uint32_t labelAt(InternalHandle handle) { return m_entries.label(handle.id()); }
    Link linkAt(InternalHandle handle) { return entryAt(handle).link; }

    OrderingSets m_sets;
    Equality m_equality;
    Unordered m_unordered;
    SymmetricBinaryRelation<Entry> m_entries;
};

}