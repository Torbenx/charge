#pragma once

#include <check/BooleanVariables.h>
#include <check/CodeBlock.h>
#include <check/CodeBlockTheory.h>
#include <check/EqualityTheory.h>
#include <check/LoadSet.h>
#include <check/Reason.h>

#include <bit>

namespace check {

struct Solver {
    using Literal = BooleanValue;

    struct SubTraceEntry {
        Literal literal;
        Reason reason;
    };
    using Conflict = SubTraceEntry;

    Solver();

    ValueTheory& getTheoryById(int_t id) {
        return *valueTheories[id];
    }

    // ValueKindTheory
    ValueKindTheory& theoryFor(ValueKind kind) {
        return *kindTheories[(int_t)kind];
    }

    Literal equality(Value a, Value b) {
        ValueKind kind = kindOf(a);
        VERIFY(kind == kindOf(b));
        return theoryFor(kind).equality(*this, a, b);
    }

    Literal disequality(Value a, Value b) {
        ValueKind kind = kindOf(a);
        VERIFY(kind == kindOf(b));
        return theoryFor(kind).disequality(*this, a, b);
    }

    Value defineLoad(MemoryLocation location, CodePosition position) {
        auto maybeKind = loadedKind(location);
        VERIFY(maybeKind.has_value());
        return theoryFor(maybeKind.value()).defineLoad(*this, location, position);
    }

    // ValueTheory
    ValueTheory& theoryFor(Value value) {
        return *valueTheories[value.theoryId];
    }

    std::string formatValue(Value value) {
        return theoryFor(value).formatValue(*this, value);
    }

    uint64_t labelOf(Value value) {
        return theoryFor(value).labelOfValue(*this, value);
    }

    ValueKind kindOf(Value value) {
        return theoryFor(value).valuesKind();
    }

    void collectInactiveReasons(Value value, std::vector<BooleanValue>& clause) {
        theoryFor(value).collectValueInactiveReasons(*this, value, clause);
    }

    bool isActive(Value value) {
        return theoryFor(value).isValueActive(*this, value);
    }

    std::strong_ordering compare(Value a, Value b) { return labelOf(a) <=> labelOf(b); }

    // BooleanTheory
    BooleanTheory& theoryFor(BooleanValue value) {
        return static_cast<BooleanTheory&>(*valueTheories[value.theoryId]);
    }

    BooleanTheory::LiteralInfo& infoFor(BooleanValue literal) {
        return theoryFor(literal).literalInfo(*this, literal);
    }

    Literal negate(Literal lit) {
        return theoryFor(lit).negate(*this, lit);
    }

    bool tentativelyTrue(Literal lit) {
        return theoryFor(lit).literalInfo(*this, lit).tentativelyTrue();
    }

    // MemoryLocationTheory
    MemoryLocationTheory& theoryFor(MemoryLocation value) {
        return static_cast<MemoryLocationTheory&>(*valueTheories[value.theoryId]);
    }

    Type typeAtLocation(MemoryLocation location) {
        return theoryFor(location).typeAtLocation(*this, location);
    }

    std::optional<ValueKind> loadedKind(MemoryLocation location) {
        return loadedKind(typeAtLocation(location));
    }

    // MemberExpressionTheory
    MemberExpressionTheory& theoryFor(MemberExpression value) {
        return static_cast<MemberExpressionTheory&>(*valueTheories[value.theoryId]);
    }

    Type memberType(MemberExpression expr) {
        return theoryFor(expr).memberType(*this, expr);
    }

    // TypeTheory
    TypeTheory& theoryFor(Type value) {
        return static_cast<TypeTheory&>(*valueTheories[value.theoryId]);
    }

    std::optional<ValueKind> loadedKind(Type type) {
        return theoryFor(type).scalarKind(*this, type);
    }

    // ReasonTheory
    ReasonTheory& theoryFor(const Reason& reason) {
        return *reasonTheories[reason.reasonTheory];
    }

    bool testReason(BooleanValue assignedLiteral, const Reason& reason) {
        return theoryFor(reason).testReason(*this, assignedLiteral, reason);
    }

    // CodeBlockTheory
    CodeBlockTheory& theoryFor(BlockId block) {
        return *blockTheories[block.theoryId];
    }

    uint64_t labelOf(BlockId block) { return theoryFor(block).labelOfBlock(*this, block); }

    BooleanValue blockActiveLiteral(BlockId block) {
        return theoryFor(block).blockActiveLiteral(*this, block);
    }

    bool isActive(BlockId block) { return assignedTrue(blockActiveLiteral(block)); }

    Value loadAtPosition(MemoryLocation location, CodePosition position) {
        return theoryFor(position.block).loadAtPosition(*this, location, position);
    }

    Value loadAtEndOfBlock(MemoryLocation location, BlockId block) {
        return theoryFor(block).loadAtEndOfBlock(*this, location, block);
    }

    std::strong_ordering compare(BlockId a, BlockId b) { return labelOf(a) <=> labelOf(b); }

    std::strong_ordering compare(CodePosition a, CodePosition b) {
        auto blockOrdering = compare(a.block, b.block);
        if (blockOrdering != 0)
            return blockOrdering;
        return a.position <=> b.position;
    }

    std::strong_ordering compare(Load a, Load b) {
        auto locOrdering = compare(a.location, b.location);
        if (locOrdering != 0)
            return locOrdering;
        return compare(a.position, b.position);
    }

    std::string formatBlockName(BlockId block) {
        return theoryFor(block).formatBlockName(*this, block);
    }
    std::string formatCodePosition(CodePosition position) {
        return theoryFor(position.block).formatCodePosition(*this, position);
    }

    std::string formatLoad(MemoryLocation location, CodePosition position) {
        return "load(" + formatValue(location) + " @ " + formatCodePosition(position) + ")";
    }

    // Solver

    bool isUnitTrue(BooleanValue value) {
        auto firstReason = infoFor(value).firstReason;
        if (!firstReason.has_value())
            return false;
        return at(firstReason.value()).reason.reasonTheory == unitReasons.theoryId();
    }
    bool isUnitFalse(BooleanValue value) { return isUnitTrue(negate(value)); }

    Reason makeImplicationReason(BooleanValue negatedPremise, BooleanValue consequence) {
        return implication.makeImplicationReason(negatedPremise, consequence);
    }

    void implicationAssignTrue(BooleanValue negatedPremise, BooleanValue consequence) {
        VERIFY(assignedFalse(negatedPremise));

        // Expensive check, but important for correctness
        // TODO: This does not work with phis currently
        /* std::vector<BooleanValue> premisInactiveReason, consequenceInactiveReason;
        collectInactiveReasons(negatedPremise, premisInactiveReason);
        collectInactiveReasons(consequence, consequenceInactiveReason);
        VERIFY(premisInactiveReason == consequenceInactiveReason);*/

        assignTrue(consequence, makeImplicationReason(negatedPremise, consequence));
    }

    Reason makeUnitReason() { return unitReasons.makeUnitReason(); }

    void unitAssignTrue(BooleanValue lit) {
        // Expensive check, but important for correctness
        std::vector<BooleanValue> inactiveReason;
        collectInactiveReasons(lit, inactiveReason);
        for (BooleanValue inactiveCondition : inactiveReason)
            VERIFY(isUnitFalse(inactiveCondition));

        assignTrue(lit, makeUnitReason());
    }

    //! Returns the current decision level of the solver
    /*!
    The decision level is equal to the number of decisions that were made minus 1.

    A level of -1 implies that no decisions were made. If a conflict is found at this level the
    problem is unsatisfiable.
    */
    int_t currentDecisionLevel() const { return (int_t)decisions.size() - 1; }

    //! Returns true if \p lit is assigned true and this assignment was propagated to the clause masks
    bool assignedTrue(Literal lit) {
        const auto& info = infoFor(lit);
        if (!info.tentativelyTrue())
            return false;
        return lit != firstPropagation && !info.prevPropagation.has_value();
    }

    bool assignedFalse(Literal lit) {
        return assignedTrue(negate(lit));
    }

    //! Append \p lit the end of the propagation queue
    /*!
    Literals added to the queue should be assigned true at the point of this operation.
    The queue will be processed by propagate() to propagate this assignment to the clause masks.
    */
    void queuePropagation(Literal lit) {
        auto& info = infoFor(lit);
        if (!firstPropagation.has_value()) {
            firstPropagation = lit;
            lastPropagation = lit;
            return;
        }
        info.prevPropagation = lastPropagation.value();
        infoFor(lastPropagation.value()).nextPropagation = lit;
        lastPropagation = lit;
    }

    //! Removes the first item from the propagation queue
    void removeFirstPropagation() {
        VERIFY(firstPropagation.has_value());
        Literal lit = firstPropagation.value();
        auto& info = infoFor(lit);
        firstPropagation = info.nextPropagation;

        if (info.nextPropagation.has_value())
            infoFor(info.nextPropagation.value()).prevPropagation = std::nullopt;
        else
            lastPropagation = std::nullopt;

        info.nextPropagation = std::nullopt;
    }

    //! Removes \p lit from the propagation queue
    void removePropagation(Literal lit) {
        auto& info = infoFor(lit);
        if (info.prevPropagation.has_value())
            infoFor(info.prevPropagation.value()).nextPropagation = info.nextPropagation;
        else
            firstPropagation = info.nextPropagation;

        if (info.nextPropagation.has_value())
            infoFor(info.nextPropagation.value()).prevPropagation = info.prevPropagation;
        else
            lastPropagation = info.prevPropagation;

        info.prevPropagation = std::nullopt;
        info.nextPropagation = std::nullopt;
    }

    //! Add a clause to the problem
    /*!
    The size of \p clause exceeds MAX_CLAUSE_SIZE it will be broken down into smaller ones by
    introducing auxilliary variables.
    */
    void addClause(std::vector<Literal> clause);

    //! Make a pair of boolean literals (X, NOT X)
    std::pair<Literal, Literal> makeBooleanPair();

    //! Decide that the given \p literal is true
    void decideTrue(Literal literal);

    //! Assign true to \p trueLit
    /*!
    This called either by decideTrue() or internally, for example by propagate() or addClause().
    */
    void assignTrue(Literal trueLit, Reason reason);

    //! Propagate the false assignments in the propagation queue to the clause masks
    /*!
    \returns true if all assignements were successfully propagated or false if a conflict arose
             during the propagation.
    */
    bool propagate();

    //! Analyze the current conflicts
    /*!
    This function will backtrack until all conflicts are resolved and learn a clause from them. If
    the conflicts cannot be resolved the problem is unsatisfiable and false is returned.
    */
    bool analyzeConflicts();

    //! Try to learn a new clause from \p conflict
    /*!
    The function uses the current subTrace that should be from the backtrack() operation that
    resolved \p conflict. When successful the function will identify the UIPs, generate new clauses
    and clear the conflicts. The function will fail if it detects that the solver is still in a
    conflict state. In this case calling propagate() will produce a conflict again.
    \returns true if successful
    */
    bool tryLearn(Conflict conflict);

    void dumpClause(int_t clauseIndex);
    void dumpClause(std::span<const Literal> clause);

    //! Revert all assignments up to and including \p level
    /*!
    This updates the subTrace member to contain a list of all 1st reasons that are no longer
    forcing in the order they appeared in the original trace. Note this doesn't always mean that
    the literal assignment was also reverted since another reason may still be forcing.
    */
    void backtrack(int_t level);

    //! Explicitly check that the invariances of the solver hold
    void checkInvariances();

    //! Check if all clauses are satisfied by the current assignment
    bool checkAssignment();

    //! Return whether the solver has any conflicts
    bool hasConflicts() const { return !conflicts.empty(); }

    //! Simplify the clause
    /*!
    This considers the unit clauses and removes literals that are always
    and detects if any literal is always true.
    \returns true, if the clause always true and thus redundant
    */
    bool simplifyClause(std::vector<Literal>& clause);

    std::vector<BooleanValue>& scratchClause() {
        m_scratchClause.clear();
        return m_scratchClause;
    }

private:
    struct Clauses : ReasonTheory {
        Clauses(Solver& solver);
        bool testReason(Solver&, BooleanValue assignedLiteral, const Reason& reason) override;
        ClauseAndIndex reasonToClause(Solver&, BooleanValue assignedLiteral, const Reason& reason) override;
        void newDecisionLevel(Solver&) override;
        void backtrack(Solver&) override;

        void propagateAssignment(Solver&, BooleanValue);
        void reapplyAssignment(Solver&, BooleanValue);
        void unapplyAssignment(Solver&, BooleanValue);
        LiteralInstance asInstance(const Reason& reason);
        Reason makeReason(int_t clauseIndex, int_t literalIndex);

        void addClause(Solver&, std::vector<Literal> clause);

        void checkInvariances(Solver&);

        //! Bitmasks for the clauses
        /*!
        The mask will contain a 1 for literals that are not false. That is for all literals that are
        either true or unassigned. If all literals in a clause except one are false, i.e. if this mask
        has exactly one bit set, the remaining literal must be true for the clause to be true.
        Detecting this case is the primary purpose of these masks.

        The updating of the masks is done in propagate() and backtrack(). propagate() will propagate
        already made false assignments to the masks by clearing the appropiate bits. While backtrack()
        will set the bits for the literals that are detected to be no longer assigned.
        */
        std::vector<clause_mask_t> clauseMasks;

        //! The actual clauses, just arrays of literals
        std::vector<std::vector<Literal>> clauses;
    };

    struct InternalVariables : BooleanVariables {
        using BooleanVariables::BooleanVariables;
        std::string formatPositiveLiteral(Solver&, int_t varId) override;
        std::string formatNegativeLiteral(Solver&, int_t varId) override;
    };

    struct BooleanEquality : EqualityTheory {
        using EqualityTheory::EqualityTheory;
        BooleanValue equality(Solver&, Value, Value) override;
        BooleanValue disequality(Solver&, Value, Value) override;
        void onNewVariable(Solver& solver, int_t varId) override;
        void propagateAssignment(Solver&, BooleanValue) override { }
        void reapplyAssignment(Solver&, BooleanValue) override { }
        void unapplyAssignment(Solver&, BooleanValue) override { }
    };

    struct BooleanLoads : SimpleBooleanTheory, LoadSet<BooleanLoads, void> {
        using SimpleBooleanTheory::SimpleBooleanTheory;
        void propagateAssignment(Solver&, BooleanValue) override { }
        void reapplyAssignment(Solver&, BooleanValue) override { }
        void unapplyAssignment(Solver&, BooleanValue) override { }
        std::string formatPositiveLiteral(Solver&, int_t) override;
        std::string formatNegativeLiteral(Solver&, int_t) override;
        uint64_t labelOfValue(Solver&, Value) override;
        void collectVariableInactiveReasons(Solver&, int_t, std::vector<BooleanValue>&) override;
        bool isVariableActive(Solver&, int_t) override;
        BooleanValue defineLoad(Solver&, MemoryLocation, CodePosition);
        void makeData(Solver&, uint32_t newHandle, MemoryLocation, CodePosition);
    };

    struct Booleans : ValueKindTheory {
        Booleans(Solver& solver)
            : m_equality(solver), m_loads(solver) { }

        BooleanValue equality(Solver&, Value, Value) override;
        BooleanValue disequality(Solver&, Value, Value) override;
        Value defineLoad(Solver&, MemoryLocation, CodePosition) override;
        std::string formatValueKind(Solver&, ValueKind) override;

        BooleanEquality m_equality;
        BooleanLoads m_loads;
    };

    struct EntryBlocks : CodeBlockTheory {
        using CodeBlockTheory::CodeBlockTheory;
        uint64_t labelOfBlock(Solver&, BlockId) override;
        Value loadAtEndOfBlock(Solver&, MemoryLocation, BlockId) override;
        Value loadAtPosition(Solver&, MemoryLocation, CodePosition) override;
        std::string formatBlockName(Solver&, BlockId) override;
        std::string formatCodePosition(Solver&, CodePosition) override;
        BooleanValue blockActiveLiteral(Solver&, BlockId) override;
    };

    struct UnitReasons : ReasonTheory {
        UnitReasons(Solver& solver)
            : ReasonTheory(solver, true) { }

        static BooleanValue reasonLiteral(const Reason& reason) {
            return std::bit_cast<BooleanValue>(reason.data1);
        }

        Reason makeUnitReason() { return { (uint32_t)theoryId() }; }
        bool testReason(Solver&, BooleanValue, const Reason&) override { return true; }
        ClauseAndIndex reasonToClause(Solver& solver, BooleanValue assignedLiteral, const Reason&) override {
            auto& clause = solver.scratchClause();
            clause.push_back(assignedLiteral);
            return { clause, 0 };
        }

        void newDecisionLevel(Solver&) override { }
        void backtrack(Solver&) override { }
    };

    struct Implication : ReasonTheory {
        Implication(Solver& solver)
            : ReasonTheory(solver, false) { }

        static BooleanValue reasonNegatedPremise(const Reason& reason) {
            return std::bit_cast<BooleanValue>(reason.data1);
        }

        Reason makeImplicationReason(BooleanValue negatedPremise, BooleanValue consequence) {
            return { (uint32_t)theoryId(), 0, std::bit_cast<uint32_t>(negatedPremise), std::bit_cast<uint32_t>(consequence) };
        }

        bool testReason(Solver& solver, BooleanValue, const Reason& reason) override {
            return solver.assignedFalse(reasonNegatedPremise(reason));
        }
        ClauseAndIndex reasonToClause(Solver& solver, BooleanValue assignedLiteral, const Reason& reason) override {
            auto& clause = solver.scratchClause();
            clause.push_back(reasonNegatedPremise(reason));
            clause.push_back(assignedLiteral);
            return { clause, 1 };
        }

        void newDecisionLevel(Solver&) override { }
        void backtrack(Solver&) override { }
    };

    //! An entry in the trace
    /*!
    Each entry represent a reason for a literal to false. The different reasons for a given literal
    are also linked together into a linked list.
    */
    struct TraceEntry {
        Literal literal; //!< Literal that is false
        Reason reason; //!< Reason the literal was assigned false
        std::optional<TracePosition> prevReason; //!< The previous reason for the assignment in trace order
        std::optional<TracePosition> nextReason; //!< The next reason for the assignment in trace order
    };

    static clause_mask_t literalMask(int_t index) { return (clause_mask_t)1 << index; }

    TraceEntry& at(TracePosition pos) {
        VERIFY(pos.index < trace.size());
        return trace[pos.index];
    }

    //! Collect all reason for \p trueLit to be true
    std::vector<Reason> collectReasons(Literal trueLit);

    friend ValueTheory;
    //! Attch a new theory to the solver
    /*!
    Called by the ValueTheory constructor.
    \returns the theory id for \p theory
    */
    int_t attachTheory(ValueTheory& theory);

    friend ReasonTheory;
    //! Attch a new theory to the solver
    /*!
    Called by the ReasonTheory constructor.
    \returns the theory id for \p theory
    */
    int_t attachTheory(ReasonTheory& theory);

    friend CodeBlockTheory;
    //! Attach a new theory to the solver
    /*!
    Called by the CodeBlockTheory constructor.
    \returns the theory id for \p theory
    */
    int_t attachTheory(CodeBlockTheory& theory);

    //! Scratch space to hold a temporary clause
    /*!
    This is useful for reason theories that lazily generate clauses.
    */
    std::vector<BooleanValue> m_scratchClause;

    //! Trace of reasons
    std::vector<TraceEntry> trace;

    //! First element in the propagation queue
    /*!
    The propagation queue is stored as a linked list intrusively inside the BooleanTheory::LiteralInfo
    (members nextPropagation and prevPropagation). It conatins the pending false assignments to be
    processed by propagate().
    */
    std::optional<Literal> firstPropagation;
    //! Last element in the propagation queue
    /*! \see firstPropagation */
    std::optional<Literal> lastPropagation;

    std::vector<Conflict> conflicts;
    std::vector<SubTraceEntry> subTrace;

    //! Used to mark literals seen by the current learn attempt
    uint32_t tryLearnIndex = 0;

    //! Positions of the decisions in the trace
    std::vector<TracePosition> decisions;

    std::vector<ValueKindTheory*> kindTheories;
    std::vector<ValueTheory*> valueTheories;
    std::vector<ReasonTheory*> reasonTheories;
    std::vector<CodeBlockTheory*> blockTheories;

    std::vector<std::unique_ptr<CodeBlock>> codeBlocks;

    // --- These variables must be initialized last since their constructors modify the theory arrays ---

    InternalVariables internalVariables;
    Clauses clauses;
    Booleans booleans;
    EntryBlocks entryBlocks;
    Implication implication;
    UnitReasons unitReasons;
};

}