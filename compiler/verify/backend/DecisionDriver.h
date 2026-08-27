#pragma once

#include <verify/backend/Solver.h>

#include <array>

namespace verify::backend {

//! Searches for a model by deciding variables
/*!
It implements VSIDS as an assignment heuristic and luby restarts.
*/
struct DecisionDriver {
    DecisionDriver(Solver&);

    GrindResult grind(Solver&);
    void bumpActivity(Solver&, Bool literal);
    void unapplyAssignment(Bool literal);
    int_t restartCount() const { return restartIndex; }

private:
    static constexpr uint32_t NOT_IN_HEAP = limits::max;

    //! The factor the activity raise of the next conflict grows by
    static constexpr double ACTIVITY_DECAY = 0.95;
    //! The activity that triggers scaling all activities back down
    static constexpr double ACTIVITY_LIMIT = 1e100;
    //! The conflicts of the shortest restart interval, scaled by the Luby sequence
    static constexpr int_t RESTART_UNIT = 32;

    struct VariableInfo {
        //! How much the variable took part in the recent conflicts, the VSIDS score
        double activity = 0;
        //! The position of the variable in \ref heap or NOT_IN_HEAP when it is not in it
        uint32_t heapPosition = NOT_IN_HEAP;
        //! Last assignment polarity
        bool phase = false;
    };

    static int_t parentOf(int_t position) { return (position - 1) / 2; }
    static int_t leftChildOf(int_t position) { return position * 2 + 1; }

    VariableInfo& infoFor(Bool variable) { return variableInfos[variable]; }

    bool inHeap(Bool variable) { return variableInfos[variable].heapPosition != NOT_IN_HEAP; }
    void placeAt(int_t position, Bool variable);
    void percolateUp(int_t position);
    void percolateDown(int_t position);
    void insert(Bool variable);
    Bool popHighestActivity();

    void collectNewVariables(Solver&);
    std::optional<Bool> pickDecision(Solver&);

    void countConflict();
    void rescaleActivities(Solver&);

    bool restartDue() const;
    void restart(Solver&, int_t baseLevel);

    SortData<VariableInfo, Sort::Boolean, 2> variableInfos;

    //! The variables that may still be decided, ordered as a heap by their activity
    std::vector<Bool> heap;

    std::array<uint32_t, std::to_underlying(TheoryId::COUNT)> collectedValues = {};

    double activityIncrement = 1.0;
    int_t conflictsUntilRestart = 0;
    int_t restartIndex = 0;
};

}
