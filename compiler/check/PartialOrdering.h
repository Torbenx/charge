#pragma once

#include <ReverseMemberPointer.h>
#include <check/StandardEquality.h>

#include <bitset>
#include <compare>

namespace check {

static_assert(std::bit_cast<std::partial_ordering>(int8_t{ -1 }) == std::partial_ordering::less);
static_assert(std::bit_cast<std::partial_ordering>(int8_t{ 0 }) == std::partial_ordering::equivalent);
static_assert(std::bit_cast<std::partial_ordering>(int8_t{ 1 }) == std::partial_ordering::greater);
static_assert(std::bit_cast<std::partial_ordering>(int8_t{ 2 }) == std::partial_ordering::unordered);

constexpr int_t toIndex(std::partial_ordering ordering) { return std::bit_cast<int8_t>(ordering) + 1; }

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
        m_set.set(toIndex(ordering), value);
    }
    constexpr void clear() { m_set.reset(); }
    constexpr int_t count() const { return m_set.count(); }
    constexpr bool test(std::partial_ordering ordering) const {
        return m_set[toIndex(ordering)];
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

    struct OrderingHandle {
        uint32_t id;
    };

    PartialOrderingTheory(Solver& solver, uint64_t baseLabel);

protected:
    PartialOrderingsSet possibleOrderings(Solver&, Value, Value) { return PartialOrderingsSet::all(); }

private:
    struct Entry {
        std::array<BooleanValue, 4> literals;
        Link link;

        BooleanValue operator[](std::partial_ordering ordering) const { return literals[toIndex(ordering)]; }
    };

    struct Equality : StandardEquality {
        using StandardEquality::StandardEquality;

        PartialOrderingTheory* theory() { return ReverseMemberPointer<&PartialOrderingTheory::m_equality>::reverse(this); }

        bool isUnitDisequal(Solver& solver, Value a, Value b) override {
            return !theory()->possibleOrderings(solver, a, b).test(std::partial_ordering::equivalent);
        }
        Link equalityLink(int_t eqId) override { return theory()->at(m_handles[eqId]).link; }
        int_t lookupEqualityVariable(Solver&, Value, Value) override;
        uint32_t labelOfVariable(Solver&, int_t varId) override;

        std::vector<OrderingHandle> m_handles;
    };

    struct Unordered : SimpleBooleanTheory {
        using SimpleBooleanTheory::SimpleBooleanTheory;

        PartialOrderingTheory* theory() { return ReverseMemberPointer<&PartialOrderingTheory::m_unordered>::reverse(this); }

        std::string formatPositiveLiteral(Solver&, int_t varId) override;
        std::string formatNegativeLiteral(Solver&, int_t varId) override;
        void propagateAssignment(Solver&, BooleanValue) override;
        void unapplyAssignment(Solver&, BooleanValue) override;
        void reapplyAssignment(Solver&, BooleanValue) override;
        uint32_t labelOfVariable(Solver&, int_t varId) override;
        bool isVariableActive(Solver&, int_t varId) override;
        void collectVariableInactiveReasons(Solver&, int_t varId, std::vector<BooleanValue>& clause) override;

        std::vector<OrderingHandle> m_handles;
    };

    void propagateAssignment(Solver&, OrderingHandle, std::partial_ordering, bool);
    void unapplyAssignment(Solver&, OrderingHandle, std::partial_ordering, bool);
    void reapplyAssignment(Solver&, OrderingHandle, std::partial_ordering, bool);

    Entry& at(OrderingHandle handle) { return m_entries[handle.id]; }
    std::pair<std::string, std::string> formatValues(Solver&, OrderingHandle);

    std::vector<Entry> m_entries;
    Equality m_equality;
    Unordered m_unordered;
};

}