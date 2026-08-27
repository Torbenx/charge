#include <verify/backend/DecisionDriver.h>

#include <gtest/gtest.h>

namespace verify::backend {

static uint64_t lubyPeroid(int_t index) {
    // The sequence is built from the finite subsequences 1 | 1 2 | 1 1 2 4 | ... whose lengths are the
    // numbers 2^k - 1. The first loop finds the subsequence \p index falls into and the second one steps
    // into the subsequences before it until \p index is the last element of the one it reached.
    int_t length = 1;
    int_t exponent = 0;
    while (length < index + 1) {
        length = length * 2 + 1;
        exponent += 1;
    }

    while (length != index + 1) {
        length = (length - 1) / 2;
        exponent -= 1;
        index = index % length;
    }

    return (uint64_t)1 << exponent;
}

TEST(VerifyBackend, DecisionLubySequence) {
    // The sequence is made of the subsequences 1 | 1 2 | 1 1 2 4 | 1 1 2 1 1 2 4 8 | ...
    std::vector<uint64_t> expected { 1, 1, 2, 1, 1, 2, 4, 1, 1, 2, 1, 1, 2, 4, 8, 1, 1, 2 };
    for (int_t index = 0; index < (int_t)expected.size(); index++)
        EXPECT_EQ(lubyPeroid(index), expected[index]);
}

DecisionDriver::DecisionDriver(Solver& solver)
    : variableInfos(solver) { }

// ------------------------------ Heap ------------------------------

void DecisionDriver::placeAt(int_t position, Bool variable) {
    heap[position] = variable;
    infoFor(variable).heapPosition = (uint32_t)position;
}

void DecisionDriver::percolateUp(int_t position) {
    Bool variable = heap[position];
    double activity = infoFor(variable).activity;

    while (position > 0) {
        int_t parent = parentOf(position);
        if (infoFor(heap[parent]).activity >= activity)
            break;
        placeAt(position, heap[parent]);
        position = parent;
    }

    placeAt(position, variable);
}

void DecisionDriver::percolateDown(int_t position) {
    Bool variable = heap[position];
    double activity = infoFor(variable).activity;

    for (;;) {
        int_t child = leftChildOf(position);
        if (child >= (int_t)heap.size())
            break;
        if (child + 1 < (int_t)heap.size() && infoFor(heap[child + 1]).activity > infoFor(heap[child]).activity)
            child += 1;
        if (infoFor(heap[child]).activity <= activity)
            break;
        placeAt(position, heap[child]);
        position = child;
    }

    placeAt(position, variable);
}

void DecisionDriver::insert(Bool variable) {
    variable = variable.baseValue();
    if (inHeap(variable))
        return;
    heap.push_back(variable);
    placeAt(heap.size() - 1, variable);
    percolateUp(heap.size() - 1);
}

Bool DecisionDriver::popHighestActivity() {
    VERIFY(!heap.empty());
    Bool result = heap.front();
    infoFor(result).heapPosition = NOT_IN_HEAP;

    Bool last = heap.back();
    heap.pop_back();
    if (result != last) {
        placeAt(0, last);
        percolateDown(0);
    }
    return result;
}

// ---------------------------- Decisions ---------------------------

void DecisionDriver::collectNewVariables(Solver& solver) {
    for (int_t index = 0; index < (int_t)TheoryId::COUNT; index++) {
        TheoryId theory = (TheoryId)index;
        if (sortOf(theory) != Sort::Boolean)
            continue;
        int_t count = solver.booleanCount(theory);
        for (int_t id = collectedValues[index]; id < count; id++) {
            Bool variable(theory, id * 2);
            // An assigned variable is put back into the heap by onUnassign() when it is reverted
            if (!solver.assignedTrue(variable) && !solver.assignedFalse(variable))
                insert(variable);
        }
        collectedValues[index] = (uint32_t)count;
    }
}

std::optional<Bool> DecisionDriver::pickDecision(Solver& solver) {
    collectNewVariables(solver);

    while (!heap.empty()) {
        Bool variable = popHighestActivity();
        if (solver.assignedTrue(variable) || solver.assignedFalse(variable))
            continue;
        return infoFor(variable).phase ? variable : !variable;
    }
    return std::nullopt;
}

void DecisionDriver::unapplyAssignment(Bool literal) {
    infoFor(literal).phase = !literal.negated();
    insert(literal);
}

// ---------------------------- Activities --------------------------

void DecisionDriver::bumpActivity(Solver& solver, Bool literal) {
    auto& info = infoFor(literal);
    info.activity += activityIncrement;
    if (info.activity > ACTIVITY_LIMIT)
        rescaleActivities(solver);

    if (info.heapPosition != NOT_IN_HEAP)
        percolateUp(info.heapPosition);
}

void DecisionDriver::countConflict() {
    conflictsUntilRestart -= 1;
    // Raising the increment instead of lowering the activities makes the increase relative,
    // so that the variables of the recent conflicts outweigh those of the older ones.
    activityIncrement /= ACTIVITY_DECAY;
}

void DecisionDriver::rescaleActivities(Solver& solver) {
    for (int_t index = 0; index < (int_t)TheoryId::COUNT; index++) {
        TheoryId theory = (TheoryId)index;
        if (sortOf(theory) != Sort::Boolean)
            continue;
        solver.forEachBoolean(theory, [this](Bool variable) {
            infoFor(variable).activity /= ACTIVITY_LIMIT;
        });
    }
    activityIncrement /= ACTIVITY_LIMIT;
}

// ----------------------------- Restarts ---------------------------

bool DecisionDriver::restartDue() const {
    return conflictsUntilRestart <= 0;
}

void DecisionDriver::restart(Solver& solver, int_t baseLevel) {
    VERIFY(solver.currentDecisionLevel() > baseLevel);
    solver.backtrack(baseLevel + 1);
    restartIndex += 1;
    conflictsUntilRestart = lubyPeroid(restartIndex) * RESTART_UNIT;
}

// ------------------------------ Search ----------------------------

GrindResult DecisionDriver::grind(Solver& solver) {
    // The decisions below this level are the assumptions of the caller, which the search neither
    // takes back nor restarts past. Reaching below it means they were refuted.
    int_t baseLevel = solver.currentDecisionLevel();

    for (;;) {
        while (!solver.propagate()) {
            countConflict();
            if (!solver.analyzeConflicts())
                return GrindResult::UnconditionallyUnsatisfiable;
            if (solver.currentDecisionLevel() < baseLevel)
                return GrindResult::AssumptionsUnsatisfiable;
        }

        if (restartDue() && solver.currentDecisionLevel() > baseLevel) {
            restart(solver, baseLevel);
            continue;
        }

        auto decision = pickDecision(solver);
        if (!decision.has_value()) {
            VERIFY(solver.checkAssignment());
            return GrindResult::Model;
        }

        solver.decideTrue(decision.value());
    }
}

}
