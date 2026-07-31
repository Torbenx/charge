#pragma once

#include <types.h>

#include <algorithm>
#include <ranges>

namespace verify::backend {

struct TracePosition {
    uint32_t index;

    constexpr explicit TracePosition(uint32_t index)
        : index(index) { }

    auto operator<=>(const TracePosition&) const = default;
    bool operator==(const TracePosition&) const = default;
    TracePosition& operator++() {
        index += 1;
        return *this;
    }
    TracePosition operator++(int) {
        TracePosition copy = *this;
        index += 1;
        return copy;
    }
    friend TracePosition operator+(TracePosition l, int_t r) {
        return TracePosition(l.index + r);
    }
    friend TracePosition operator-(TracePosition l, int_t r) {
        return TracePosition(l.index - r);
    }
    TracePosition& operator+=(int_t r) {
        index += r;
        return *this;
    }
    TracePosition& operator-=(int_t r) {
        index -= r;
        return *this;
    }
};

}

template<>
struct optional_traits<verify::backend::TracePosition> {
    static constexpr verify::backend::TracePosition empty_value = verify::backend::TracePosition(limits::max);
};

namespace verify::backend {

namespace TraceDetail {

    //! The index a trace position refers to
    template<typename Position>
    constexpr uint32_t indexOf(Position position) {
        if constexpr (std::integral<Position>)
            return position;
        else if constexpr (requires { position.index; })
            return position.index;
        else
            return position.id();
    }

}

//! A trace of the entries recorded at the decision levels of the solver
template<typename T, typename Position = TracePosition>
struct Trace {
    int_t size() const { return (int_t)entries.size(); }
    bool empty() const { return entries.empty(); }

    //! The position the next entry appended will get
    Position nextPosition() const { return Position((uint32_t)entries.size()); }

    T& operator[](Position position) {
        VERIFY(TraceDetail::indexOf(position) < entries.size());
        return entries[TraceDetail::indexOf(position)];
    }
    const T& operator[](Position position) const {
        VERIFY(TraceDetail::indexOf(position) < entries.size());
        return entries[TraceDetail::indexOf(position)];
    }

    T& back() { return entries.back(); }
    const T& back() const { return entries.back(); }

    auto begin() { return entries.begin(); }
    auto begin() const { return entries.begin(); }
    auto end() { return entries.end(); }
    auto end() const { return entries.end(); }
    auto allPositions() const { return positions(0, size()); }

    //! Append \p entry to the trace and return its position
    Position push(T entry) {
        Position position = nextPosition();
        entries.push_back(std::move(entry));
        return position;
    }

    //! Append an entry constructed from \p args to the trace and return its position
    template<typename... Args>
    Position emplace(Args&&... args) {
        Position position = nextPosition();
        entries.emplace_back(std::forward<Args>(args)...);
        return position;
    }

    void newDecisionLevel(Solver& solver) {
        decisionPoints.push_back((uint32_t)entries.size());
        VERIFY((int_t)decisionPoints.size() == solver.currentDecisionLevel() + 1);
    }

    //! The position of the first entry the backtrack reverts
    Position backtrackedBegin(Solver& solver) const { return Position(backtrackedIndex(solver)); }

    //! The entries the backtrack reverts, in the order they were recorded
    std::span<T> backtrackedChrono(Solver& solver) {
        return std::span<T>(entries).subspan(backtrackedIndex(solver));
    }
    //! The entries the backtrack reverts, in reverse order
    auto backtrackedReverse(Solver& solver) { return std::views::reverse(backtrackedChrono(solver)); }

    //! The positions of backtrackedChrono()
    auto backtrackedPositionsChrono(Solver& solver) {
        return positions(backtrackedIndex(solver), (uint32_t)entries.size());
    }
    //! The positions of backtrackedReverse()
    auto backtrackedPositionsReverse(Solver& solver) {
        return std::views::reverse(backtrackedPositionsChrono(solver));
    }

    //! Drop the entries the backtrack reverts together with their decision points
    void truncate(Solver& solver) {
        int_t lastLevelToRevert = solver.currentDecisionLevel() + 1;
        entries.erase(entries.begin() + backtrackedIndex(solver), entries.end());
        decisionPoints.resize(lastLevelToRevert);
    }

    //! Explicitly check that the invariances of the trace hold
    void checkInvariances(Solver& solver) const {
        // The entries of a level are appended after its decision point, so the points only grow
        VERIFY((int_t)decisionPoints.size() == solver.currentDecisionLevel() + 1);
        VERIFY(std::ranges::is_sorted(decisionPoints));
        VERIFY(decisionPoints.empty() || decisionPoints.back() <= entries.size());
    }

private:
    uint32_t backtrackedIndex(Solver& solver) const {
        int_t lastLevelToRevert = solver.currentDecisionLevel() + 1;
        VERIFY(lastLevelToRevert < (int_t)decisionPoints.size());
        return decisionPoints[lastLevelToRevert];
    }

    static auto positions(uint32_t first, uint32_t last) {
        return std::views::iota(first, last)
            | std::views::transform([](uint32_t index) { return Position(index); });
    }

    std::vector<T> entries;
    std::vector<uint32_t> decisionPoints; //!< Trace sizes at the respective decision levels
};

}
