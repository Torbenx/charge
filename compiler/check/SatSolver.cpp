#include <check/MemberExpressions.h>
#include <check/MemoryLocations.h>
#include <check/SatSolver.h>
#include <check/StandardEquality.h>
#include <check/Types.h>

#include <utility>

namespace check {

std::byte* Solver::allocateAndFillData(int_t theoryId, const ValueTheoryInfo& theoryInfo, const CommonDataInfo& dataInfo) {
    if (theoryInfo.dataCapacity == 0)
        return nullptr;

    VERIFY(theoryInfo.valueCount <= theoryInfo.dataCapacity);
    std::allocator<std::byte> alloc;
    std::byte* result = alloc.allocate((size_t)dataInfo.elementSize * (size_t)theoryInfo.dataCapacity);
    for (int_t i = 0; i < (int_t)theoryInfo.valueCount; i++) {
        dataInfo.initFunction(result + i * dataInfo.elementSize, Value { (uint32_t)theoryId, (uint32_t)i });
    }
    return result;
}

void Solver::moveData(std::byte*& data, const CommonDataInfo& dataInfo, int_t oldCapacity, int_t newCapacity) {
    std::allocator<std::byte> alloc;
    std::byte* newData = alloc.allocate((size_t)newCapacity * (size_t)dataInfo.elementSize);
    if (oldCapacity == 0) {
        VERIFY(data == nullptr);
    } else {
        size_t oldByteCount = (size_t)oldCapacity * (size_t)dataInfo.elementSize;
        std::copy_n(data, oldByteCount, newData);
        alloc.deallocate(data, oldByteCount);
    }
    data = newData;
}

void Solver::deallocateAndDestroyData(std::byte*& data, const ValueTheoryInfo& theoryInfo, const CommonDataInfo& dataInfo) {
    if (theoryInfo.dataCapacity == 0) {
        VERIFY(data == nullptr);
        return;
    }
    for (int_t i = 0; i < (int_t)theoryInfo.valueCount; i++) {
        dataInfo.destroyFunction(data + i * dataInfo.elementSize);
    }
    std::allocator<std::byte> alloc;
    alloc.deallocate(data, (size_t)theoryInfo.dataCapacity * (size_t)dataInfo.elementSize);
    data = nullptr;
}

TheoryDataBase::TheoryDataBase(Solver& solver, ValueTheory& theory, int_t elementSize, DataInitializeFunction initFunction, DataDestroyFunction destroyFunction) {
    solver.registerTheoryData(*this, theory.theoryId(), elementSize, initFunction, destroyFunction);
}

void Solver::registerTheoryData(TheoryDataBase& data, int_t theoryId, int_t elementSize, DataInitializeFunction initFunction, DataDestroyFunction destroyFunction) {
    auto& info = valueTheories[theoryId];
    auto& dataInfo = info.datas.emplace_back(CommonDataInfo { elementSize, initFunction, destroyFunction }, &data);
    VERIFY(data.m_pointer == nullptr);
    dataInfo.pointer = allocateAndFillData(theoryId, info, dataInfo);
    data.m_pointer = dataInfo.pointer;
}

KindDataBase::KindDataBase(Solver& solver, ValueKind kind, int_t elementSize, DataInitializeFunction initFunction, DataDestroyFunction destroyFunction) {
    solver.registerKindData(*this, kind, elementSize, initFunction, destroyFunction);
}

void Solver::registerKindData(KindDataBase& data, ValueKind kind, int_t elementSize, DataInitializeFunction initFunction, DataDestroyFunction destroyFunction) {
    auto& info = kindTheories[std::to_underlying(kind)];
    auto& dataInfo = info.datas.emplace_back(CommonDataInfo { elementSize, initFunction, destroyFunction }, &data);
    VERIFY(data.m_table == nullptr);
    VERIFY(theoryTableCapacity > 0);
    std::allocator<std::byte*> alloc;
    dataInfo.table = alloc.allocate(theoryTableCapacity);
    std::uninitialized_fill_n(dataInfo.table, theoryTableCapacity, nullptr);
    for (int_t i = 0; i < (int_t)valueTheories.size(); i++) {
        const auto& theoryInfo = valueTheories[i];
        if (theoryInfo.theory->valuesKind() == kind) {
            dataInfo.table[i] = allocateAndFillData(i, theoryInfo, dataInfo);
        }
    }
    data.m_table = dataInfo.table;
}

Value ValueTheory::newValue(Solver& solver) {
    return solver.handleNewValue(theoryId());
}

Value Solver::handleNewValue(int_t theoryId) {
    auto& theoryInfo = valueTheories[theoryId];
    int_t valueId = theoryInfo.valueCount++;
    Value resultValue { (uint32_t)theoryId, (uint32_t)valueId };
    if (theoryInfo.valueCount > theoryInfo.dataCapacity) {
        int_t oldCapacity = theoryInfo.dataCapacity;
        int_t newCapacity = std::max<int_t>(oldCapacity * 2, 4);
        theoryInfo.dataCapacity = newCapacity;
        for (auto& dataInfo : theoryInfo.datas) {
            moveData(dataInfo.pointer, dataInfo, oldCapacity, newCapacity);
            dataInfo.base->m_pointer = dataInfo.pointer;
        }
        auto& kindInfo = kindTheories[std::to_underlying(theoryInfo.theory->valuesKind())];
        for (auto& dataInfo : kindInfo.datas) {
            moveData(dataInfo.table[theoryId], dataInfo, oldCapacity, newCapacity);
        }
    }
    for (auto& dataInfo : theoryInfo.datas) {
        dataInfo.initFunction(dataInfo.pointer + valueId * dataInfo.elementSize, resultValue);
    }
    auto& kindInfo = kindTheories[std::to_underlying(theoryInfo.theory->valuesKind())];
    for (auto& dataInfo : kindInfo.datas) {
        dataInfo.initFunction(dataInfo.table[theoryId] + valueId * dataInfo.elementSize, resultValue);
    }
    return resultValue;
}

ValueTheory::ValueTheory(Solver& solver, ValueKind kind)
    : m_valuesKind(kind), m_theoryId(solver.attachTheory(*this)) { }

int_t Solver::attachTheory(ValueTheory& theory) {
    VERIFY((int_t)theory.valuesKind() < (int_t)kindTheories.size());
    VERIFY(kindTheories[(int_t)theory.valuesKind()].theory != nullptr);

    int_t id = valueTheories.size();
    valueTheories.push_back({ &theory });
    if ((int_t)valueTheories.size() > theoryTableCapacity) {
        int_t oldCapacity = theoryTableCapacity;
        theoryTableCapacity *= 2;
        for (auto& kindInfo : kindTheories) {
            for (auto& dataInfo : kindInfo.datas) {
                // The new theory starts out with 0 values so a nullptr entry for it is fine
                std::allocator<std::byte*> alloc;
                std::byte** newTable = alloc.allocate(theoryTableCapacity);
                std::copy_n(dataInfo.table, oldCapacity, newTable);
                std::uninitialized_fill_n(newTable + oldCapacity, theoryTableCapacity - oldCapacity, nullptr);
                alloc.deallocate(dataInfo.table, oldCapacity);
                dataInfo.table = newTable;
                dataInfo.base->m_table = newTable;
            }
        }
    }
    return id;
}

Solver::~Solver() {
    // Note: We must access the Theory/KindDataBase since they may already be deallocated.
    for (auto& theoryInfo : valueTheories) {
        if (theoryInfo.dataCapacity == 0)
            continue;
        for (auto& dataInfo : theoryInfo.datas) {
            VERIFY(dataInfo.pointer != nullptr);
            deallocateAndDestroyData(dataInfo.pointer, theoryInfo, dataInfo);
        }
    }
    for (auto& kindInfo : kindTheories) {
        for (auto& dataInfo : kindInfo.datas) {
            for (int_t i = 0; i < (int_t)valueTheories.size(); i++) {
                if (dataInfo.table[i] != nullptr) {
                    deallocateAndDestroyData(dataInfo.table[i], valueTheories[i], dataInfo);
                }
            }
            std::allocator<std::byte*> alloc;
            alloc.deallocate(dataInfo.table, theoryTableCapacity);
            dataInfo.table = nullptr;
        }
    }
}

ValueKindTheory::ValueKindTheory(Solver& solver, ValueKind kind) {
    solver.attachTheory(*this, kind);
}

void Solver::attachTheory(ValueKindTheory& theory, ValueKind kind) {
    int_t kindId = (int_t)kind;
    if (kindId >= (int_t)kindTheories.size())
        kindTheories.resize(kindId + 1);

    VERIFY(kindTheories[kindId].theory == nullptr);
    kindTheories[kindId].theory = &theory;
}

ReasonTheory::ReasonTheory(Solver& solver, bool propagating)
    : m_theoryId(solver.attachTheory(*this)), m_propagating(propagating) {
    VERIFY(solver.currentDecisionLevel() == -1);
}

int_t Solver::attachTheory(ReasonTheory& theory) {
    int_t id = reasonTheories.size();
    reasonTheories.push_back(&theory);
    return id;
}

CodeBlockTheory::CodeBlockTheory(Solver& solver)
    : m_theoryId(solver.attachTheory(*this)) { }

int_t Solver::attachTheory(CodeBlockTheory& theory) {
    int_t id = blockTheories.size();
    blockTheories.push_back(&theory);
    return id;
}

OrientedPair OrientedPair::orient(Solver& solver, Value a, Value b) {
    if (solver.compare(a, b) > 0)
        std::swap(a, b);
    return { a, b };
}

ValueBaseLabel::ValueBaseLabel(Solver& solver, ValueCategory category) {
    solver.attachBaseLabel(*this, category);
}

void Solver::attachBaseLabel(ValueBaseLabel& label, ValueCategory category) {
    uint16_t counter = ++baseLabelCounter;
    baseLabels.emplace(BaseLabelOrderingKey { category, counter }, &label);
    // Relabel all base labels.
    static constexpr uint64_t INCREMENT = (uint64_t)1 << 48;
    uint64_t value = INCREMENT;
    for (auto [key, baseLabel] : baseLabels) {
        baseLabel->baseLabel = value;
        value += INCREMENT;
    }
}

// ----------------------------- Clauses ----------------------------

Solver::Clauses::Clauses(Solver& solver)
    : ReasonTheory(solver, true) { }

Reason Solver::Clauses::makeReason(int_t clauseIndex, int_t literalIndex) {
    return { .reasonTheory = (uint32_t)theoryId(), .data0 = (uint32_t)literalIndex, .data1 = (uint32_t)clauseIndex };
}

bool Solver::Clauses::testReason(Solver&, BooleanValue, const Reason& reason) {
    int_t clauseIndex = reason.data1;
    return std::popcount(clauseMasks[clauseIndex]) == 1;
}

ReasonTheory::ClauseAndIndex Solver::Clauses::reasonToClause(Solver&, BooleanValue, const Reason& reason) {
    int_t clauseIndex = reason.data1;
    int_t literalIndex = reason.data0;
    return { clauses[clauseIndex], literalIndex };
}

LiteralInstance Solver::Clauses::asInstance(const Reason& reason) {
    int_t clauseIndex = reason.data1;
    int_t literalIndex = reason.data0;
    return { (uint32_t)literalIndex, (uint32_t)clauseIndex };
}

void Solver::Clauses::newDecisionLevel(Solver&) { }

void Solver::Clauses::backtrack(Solver&) { }

void Solver::Clauses::reapplyAssignment(Solver&, BooleanValue) { }

void Solver::Clauses::propagateAssignment(Solver& solver, BooleanValue literal) {
    const auto& info = solver.infoFor(!literal);
    // VERIFY(info.assignedFalse());
    // VERIFY(!literalTheory.getInfo(literalTheory.negate(literal)).assignedFalse());

    for (auto inst : info.instances) {
        clause_mask_t& clauseMask = clauseMasks[inst.clauseIndex];
        // Perform the popcount before we clear the bit so the operations can be executed in parallel
        int popcnt = std::popcount(clauseMask);

        // solver.dumpClause(solver.clauses[inst.clauseIndex]);
        // println("{:#032b} - {:#032b} = {:#032b}", clauseMask, literalMask(inst.literalIndex), clauseMask & ~literalMask(inst.literalIndex));

        // VERIFY((clauseMask & literalMask(inst.literalIndex)) != (clause_mask_t)0);
        clauseMask &= ~literalMask(inst.literalIndex);

        // Detect if the clause has only one non-false literal (only one bit set).
        // Since popcnt still counts the bit we just cleared we must test against 2 instead of 1.
        if (popcnt > 2)
            continue;

        VERIFY(popcnt == 2);

        // Unit clause propagation:
        // All other literals in this clause are false thus the last one must be true.
        const auto& clause = clauses[inst.clauseIndex];

        int_t trueLitIndex = std::countr_zero(clauseMask);
        Literal trueLit = clause[trueLitIndex];
        solver.assignTrue(trueLit, makeReason(inst.clauseIndex, trueLitIndex));
    }
}

void Solver::Clauses::unapplyAssignment(Solver& solver, BooleanValue literal) {
    const auto& info = solver.infoFor(!literal);
    for (auto inst : info.instances) {
        auto& clauseMask = clauseMasks[inst.clauseIndex];
        auto mask = literalMask(inst.literalIndex);
        clauseMask |= mask;
    }
}

void Solver::Clauses::addClause(Solver& solver, std::vector<Literal> clause) {
    VERIFY(!clause.empty());
    VERIFY((int_t)clause.size() <= MAX_CLAUSE_SIZE);
    VERIFY(clauses.size() == clauseMasks.size());
    int_t clauseIndex = clauses.size();
    clause_mask_t mask = 0;
    for (int_t index = 0; index < (int_t)clause.size(); index++) {
        LiteralInstance inst { (uint32_t)index, (uint32_t)clauseIndex };
        Literal lit = clause[index];
        solver.infoFor(lit).instances.push_back(inst);
        if (!solver.assignedFalse(lit))
            mask |= literalMask(index);
    }
    VERIFY(mask != 0);
    clauses.emplace_back(std::move(clause));
    clauseMasks.push_back(mask);
    if (std::popcount(mask) == 1) {
        int_t index = std::countr_zero(mask);
        solver.assignTrue(clauses.back()[index], makeReason(clauseIndex, index));
    }
}

// ------------------------ InternalVariables -----------------------

std::string Solver::InternalVariables::formatPositiveLiteral(Solver&, int_t varId) {
    if (varId == 0)
        return "true";
    return "internal" + std::to_string(varId);
}
std::string Solver::InternalVariables::formatNegativeLiteral(Solver&, int_t varId) {
    if (varId == 0)
        return "false";
    return "!internal" + std::to_string(varId);
}

// ------------------------- BooleanEquality ------------------------

BooleanValue Solver::BooleanEquality::equality(Solver& solver, Value va, Value vb) {
    BooleanValue a { va };
    BooleanValue b { vb };

    if (a == b)
        return builtins::true_literal;
    if (a == solver.negate(b))
        return builtins::false_literal;

    if (solver.isUnitTrue(a))
        return b;
    if (solver.isUnitTrue(b))
        return a;
    if (solver.isUnitFalse(a))
        return solver.negate(b);
    if (solver.isUnitFalse(b))
        return solver.negate(a);

    return positiveLiteral(equalityVariable(solver, a, b));
}

BooleanValue Solver::BooleanEquality::disequality(Solver& solver, Value va, Value vb) {
    BooleanValue a { va };
    BooleanValue b { vb };

    if (a == b)
        return builtins::false_literal;
    if (a == solver.negate(b))
        return builtins::true_literal;

    if (solver.isUnitTrue(a))
        return solver.negate(b);
    if (solver.isUnitTrue(b))
        return solver.negate(a);
    if (solver.isUnitFalse(a))
        return b;
    if (solver.isUnitFalse(b))
        return a;

    return negativeLiteral(equalityVariable(solver, a, b));
}

int_t Solver::BooleanEquality::equalityVariable(Solver& solver, Value a, Value b) {
    Link link = Link::orient(solver, a, b);
    int_t varId = m_equalities.get(solver, link);
    if (varId == variableCount(solver)) {
        newVariable(solver);

        /*
        Equalities are eagerly encoded as clauses.
        For each equality there will be 4 clauses:
            a != b || !a ||  b
            a != b ||  a || !b
            a == b ||  a ||  b
            a == b || !a || !b
        */
        BooleanValue eq = positiveLiteral(varId);
        BooleanValue neq = negativeLiteral(varId);
        BooleanValue a { link.source };
        BooleanValue b { link.target };
        BooleanValue na = solver.negate(a);
        BooleanValue nb = solver.negate(b);

        VERIFY(a != b);
        VERIFY(a != nb);
        solver.addClause({ neq, na, b });
        solver.addClause({ neq, a, nb });
        solver.addClause({ eq, a, b });
        solver.addClause({ eq, na, nb });
    }
    return varId;
}

uint32_t Solver::BooleanEquality::labelOfVariable(Solver&, int_t varId) {
    return m_equalities.label(varId);
}

OrientedPair Solver::BooleanEquality::equalityLink(int_t varId) {
    return m_equalities.at(varId);
}

// -------------------------- BooleanLoads --------------------------

std::string Solver::BooleanLoads::formatPositiveLiteral(Solver& solver, int_t varId) {
    auto [location, position] = loadAt(varId);
    return solver.formatLoad(location, position);
}

std::string Solver::BooleanLoads::formatNegativeLiteral(Solver& solver, int_t varId) {
    return "!" + formatPositiveLiteral(solver, varId);
}

uint32_t Solver::BooleanLoads::labelOfVariable(Solver&, int_t varId) {
    return LoadSet::label(varId);
}

void Solver::BooleanLoads::collectVariableInactiveReasons(Solver& solver, int_t varId, std::vector<BooleanValue>& clause) {
    collectLoadInactiveReasons(solver, varId, clause);
}

bool Solver::BooleanLoads::isVariableActive(Solver& solver, int_t varId) {
    return isLoadActive(solver, varId);
}

BooleanValue Solver::BooleanLoads::defineLoad(Solver& solver, MemoryLocation location, CodePosition position) {
    return positiveLiteral(get(solver, location, position));
}

void Solver::BooleanLoads::makeData(Solver& solver, uint32_t newHandle, MemoryLocation, CodePosition) {
    VERIFY((int_t)newHandle == variableCount(solver));
    newVariable(solver);
}

// ---------------------------- Booleans ----------------------------

BooleanValue Solver::Booleans::equality(Solver& solver, Value a, Value b) {
    return m_equality.equality(solver, a, b);
}

Value Solver::Booleans::defineLoad(Solver& solver, MemoryLocation location, CodePosition position) {
    return m_loads.defineLoad(solver, location, position);
}

std::string Solver::Booleans::formatValueKind(Solver&, ValueKind) { return "bool"; }

// -------------------- MemoryDeclarationEquality -------------------

struct Solver::MemoryDeclarationEquality : BasicEquality {
    MemoryDeclarationEquality(Solver& solver)
        : BasicEquality(solver, ValueKind::MemoryDeclaration) { }

    bool isUnitDisequal(Solver& solver, Value a, Value b) override {
        return solver.declarationInfo((MemoryDeclaration)a).has_value()
            && solver.declarationInfo((MemoryDeclaration)b).has_value();
    }
};

// ----------------------- MemoryDeclarations -----------------------

Solver::MemoryDeclarations::MemoryDeclarations(Solver& solver)
    : ValueKindTheory(solver, ValueKind::MemoryDeclaration)
    , m_equality(std::make_unique<MemoryDeclarationEquality>(solver)) { }

Solver::MemoryDeclarations::~MemoryDeclarations() = default;

BooleanValue Solver::MemoryDeclarations::equality(Solver& solver, Value a, Value b) {
    return m_equality->equality(solver, a, b);
}

Value Solver::MemoryDeclarations::defineLoad(Solver&, MemoryLocation, CodePosition) {
    VERIFY_NOT_REACHED();
}

std::string Solver::MemoryDeclarations::formatValueKind(Solver&, ValueKind) { return "memory-declaration"; }

// --------------------------- EntryBlocks --------------------------

uint64_t Solver::EntryBlocks::labelOfBlock(Solver&, BlockId) { return 0; }

Value Solver::EntryBlocks::loadAtEndOfBlock(Solver& solver, MemoryLocation location, BlockId block) {
    return loadAtPosition(solver, location, { block, 0 });
}

Value Solver::EntryBlocks::loadAtPosition(Solver& solver, MemoryLocation location, CodePosition position) {
    return solver.defineLoad(location, position);
}

BooleanValue Solver::EntryBlocks::blockActiveLiteral(Solver&, BlockId) {
    // Since the entry block has no parents this doesn't really matter,
    // but in principle it should be active as long as some block is active.
    return builtins::true_literal;
}

std::string Solver::EntryBlocks::formatBlockName(Solver&, BlockId) { return "entry"; }

std::string Solver::EntryBlocks::formatCodePosition(Solver&, CodePosition) { return "entry"; }

// ----------------------------- Helpers ----------------------------

PartialOrderingsSet Solver::possibleOrderings(Type a, Type b) {
    return Types::possibleOrderings(*this, a, b);
}

MemberExpressions& Solver::memberExpressions() {
    return *m_memberExpressions;
}

PartialOrderingsSet Solver::possibleOrderings(MemberExpression a, MemberExpression b) {
    return MemberExpressions::possibleOrderings(*this, a, b);
}

PartialOrderingsSet Solver::possibleOrderings(MemoryLocation a, MemoryLocation b) {
    return MemoryLocations::possibleOrderings(*this, a, b);
}

// ----------------------------- Solver -----------------------------

Solver::Solver()
    : m_booleans(*this)
    , internalVariables(*this)
    , m_memoryDeclarations(*this)
    , m_types(std::make_unique<Types>(*this))
    , m_memberExpressions(std::make_unique<MemberExpressions>(*this))
    , m_memoryLocations(std::make_unique<MemoryLocations>(*this))
    , m_clauses(*this)
    , m_entryBlocks(*this)
    , implication(*this)
    , unitReasons(*this)
    , literalInfos(*this, ValueKind::Boolean) {
    {
        VERIFY(internalVariables.theoryId() == SOLVER_INTERNAL_VARS_THEORY_ID);
        int_t id = internalVariables.newVariable(*this);
        VERIFY(internalVariables.positiveLiteral(id) == builtins::true_literal);
        VERIFY(internalVariables.negativeLiteral(id) == builtins::false_literal);
        addClause({ builtins::true_literal });
    }
    {
        VERIFY(m_entryBlocks.theoryId() == ENTRY_BLOCKS_THEORY_ID);
    }
}

std::pair<BooleanValue, BooleanValue> Solver::makeBooleanPair() {
    int_t varId = internalVariables.newVariable(*this);
    return { internalVariables.positiveLiteral(varId), internalVariables.negativeLiteral(varId) };
}

bool Solver::simplifyClause(std::vector<Literal>& clause) {
    bool seenUnitTrue = false;
    auto newEnd = std::partition(clause.begin(), clause.end(), [&](Literal lit) {
        if (isUnitTrue(lit))
            seenUnitTrue = true;
        return !isUnitFalse(lit);
    });
    if (seenUnitTrue)
        return true;

    // The clause can end up empty when all literals are false due to unit clauses.
    // In that case the problem is always unsatisfiable, but the clause must kept to determine that.
    if (newEnd != clause.begin())
        clause.erase(newEnd, clause.end());

    return false;
}

void Solver::addClause(std::vector<Literal> clause) {
    for (int_t i = 0; i < (int_t)clause.size(); i++)
        collectInactiveReasons(clause[i], clause);

    if (simplifyClause(clause))
        return;

    auto comp = [this](Literal a, Literal b) { return compare(a, b) < 0; };
    std::sort(clause.begin(), clause.end(), comp);
    auto newEnd = std::unique(clause.begin(), clause.end());
    clause.erase(newEnd, clause.end());

    if (clause.size() == 1) {
        unitAssignTrue(clause[0]);
        return;
    }

    VERIFY((int_t)clause.size() <= MAX_CLAUSE_SIZE * (MAX_CLAUSE_SIZE - 1));
    if ((int_t)clause.size() <= MAX_CLAUSE_SIZE) {
        m_clauses.addClause(*this, std::move(clause));
        return;
    }
    // clause.size <= (MAX_CLAUSE_SIZE - extraClauses) + extraClauses * (MAX_CLAUSE_SIZE - 1)
    // -> extraClauses >= (clause.size - MAX_CLAUSE_SIZE) / (MAX_CLAUSE_SIZE - 2)
    // -> extraClauses >= floor( (clause.size - MAX_CLAUSE_SIZE + MAX_CLAUSE_SIZE - 3) / (MAX_CLAUSE_SIZE - 2)
    int_t extraClauses = ((int_t)clause.size() - 3) / (MAX_CLAUSE_SIZE - 2);

    // println("packing {} literals into {} clauses", clause.size(), extraClauses + 1);
    // print("clause: "); dumpClause(clause);

    int_t takenCount = 0;
    auto take = [&](std::vector<Literal>& into, int_t n) {
        VERIFY((int_t)clause.size() > takenCount);
        n = std::min(n, (int_t)clause.size() - takenCount);
        for (int_t i = 0; i < n; i++)
            into.push_back(clause[takenCount++]);
    };

    std::vector<Literal> primaryClause;
    primaryClause.reserve(MAX_CLAUSE_SIZE);
    take(primaryClause, MAX_CLAUSE_SIZE - extraClauses);

    for (int_t i = 0; i < extraClauses; i++) {
        std::vector<Literal> extraClause;
        extraClause.reserve(MAX_CLAUSE_SIZE);
        auto [posLit, negLit] = makeBooleanPair();
        primaryClause.push_back(posLit);
        extraClause.push_back(negLit);
        take(extraClause, MAX_CLAUSE_SIZE - 1);
        VERIFY(extraClause.size() >= 3);
        // print("extra: "); dumpClause(extraClause);
        m_clauses.addClause(*this, std::move(extraClause));
    }

    VERIFY(primaryClause.size() == MAX_CLAUSE_SIZE);
    // print("primary: "); dumpClause(primaryClause);
    m_clauses.addClause(*this, std::move(primaryClause));

    VERIFY(takenCount == (int_t)clause.size());
}

void Solver::decideTrue(Literal literal) {
    VERIFY(!firstPropagation.has_value());
    VERIFY(!tentativelyTrue(negate(literal)));
    decisions.push_back(TracePosition(trace.size()));
    assignTrue(literal, Reason::makeDecision(currentDecisionLevel()));
    for (auto& theory : reasonTheories)
        theory->newDecisionLevel(*this);
}

void Solver::assignTrue(Literal trueLit, Reason reason) {
    /*if (reason.isDecision()) {
        println("deciding {}", formatValue(trueLit));
    } else {
        print("assigning {}, reason: ", formatValue(trueLit));
        dumpClause(theoryFor(reason).reasonToClause(*this, reason).clause);
    }*/

    auto& info = infoFor(trueLit);
    TracePosition tracePos(trace.size());
    trace.push_back({ trueLit, reason, info.lastReason, std::nullopt });
    if (info.lastReason.has_value())
        at(*info.lastReason).nextReason = tracePos;
    info.lastReason = tracePos;

    if (!info.firstReason.has_value()) {
        info.firstReason = tracePos;
        queuePropagation(trueLit);
    }

    if (infoFor(!trueLit).tentativelyTrue()) {
        conflicts.push_back({ trueLit, reason });
    }
}

bool Solver::propagate() {
    VERIFY(conflicts.empty());

    while (firstPropagation.has_value()) {
        Literal literal = firstPropagation.value();
        auto& literalTheory = theoryFor(literal);
        // println("propagating {}", literalTheory.formatValue(*this, literal));
        removeFirstPropagation();

        literalTheory.propagateAssignment(*this, literal);
        m_clauses.propagateAssignment(*this, literal);

        if (!conflicts.empty())
            return false;
    }
    return true;
}

void Solver::dumpClause(int_t clauseIndex) {
    dumpClause(m_clauses.clauses[clauseIndex]);
}
void Solver::dumpClause(std::span<const Literal> clause) {
    for (auto lit : clause)
        print("{} ", formatValue(lit));
    println("");
}

bool Solver::tryLearn(Conflict conflict) {
    VERIFY(conflicts.empty());
    VERIFY(!subTrace.empty());
    SubTraceEntry conflictDecision = subTrace.front();
    VERIFY(conflictDecision.reason.isDecision());
    if (infoFor(conflictDecision.literal).tentativelyTrue()) {
        // If the decision was not reverted, propagating it will lead to a conflict
        return false;
    }
    tryLearnIndex += 1;

    [[maybe_unused]] auto wasReversed = [&](Literal lit) {
        return std::find_if(subTrace.begin(), subTrace.end(), [lit](SubTraceEntry entry) { return entry.literal == lit; }) != subTrace.end();
    };
    [[maybe_unused]] auto wasTrue = [&](Literal lit) { return wasReversed(lit) || infoFor(lit).tentativelyTrue(); };
    [[maybe_unused]] auto wasFalse = [&](Literal lit) { return wasTrue(negate(lit)); };

    std::vector<Literal> newClause;
    int_t position = subTrace.size();
    int_t openLiterals = 0;
    bool seenSinglePropagatingReason = true;
    std::vector<bool> shouldBeVisited;
    shouldBeVisited.resize(subTrace.size());

    {
        auto [conflictClause, conflictLiteralIndex] = theoryFor(conflict.reason).reasonToClause(*this, conflict.literal, conflict.reason);
        for (Literal falseLit : conflictClause) {
            Literal trueLit = !falseLit;
            auto& info = infoFor(trueLit);

            VERIFY(wasTrue(trueLit));

            if (info.tentativelyTrue()) {
                // TODO: Storing the 'includedInNewClause' guard in the opposite literal is not so nice
                VERIFY(info.includedInNewClause != tryLearnIndex);
                newClause.push_back(falseLit);
                info.includedInNewClause = tryLearnIndex;
            } else {
                VERIFY(!shouldBeVisited[info.subTraceIndex]);
                openLiterals += 1;
                shouldBeVisited[info.subTraceIndex] = true;
            }
        }
        if (openLiterals == 0) {
            // All literals in the clause were false, this is a conflict.
            return false;
        }
        if (!theoryFor(conflict.reason).isPropagating())
            seenSinglePropagatingReason = false;
    }

    for (;;) {
        for (;;) {
            position -= 1;
            if (shouldBeVisited[position])
                break;
            if (position == 0) {
                // Entry 0 of the subTrace is always the decision of the reversed level. When we
                // get here we iterated though the entire trace without marking the decision to be
                // visited and thus it is not part of the implication graph that lead to the
                // conflict.
                return false;
            }
        }

        SubTraceEntry entry = subTrace[position];
        openLiterals -= 1;
        if (openLiterals == 0) {
            // Found a UIP
            VERIFY(!infoFor(entry.literal).tentativelyTrue());

            // Add the new clause but only if it doesn't exists jet
            if (!seenSinglePropagatingReason) {
                newClause.push_back(negate(entry.literal));
                // print("learning: "); dumpClause(newClause);
                addClause(std::move(newClause));
                VERIFY(conflicts.empty());
            }

            if (entry.reason.isDecision()) {
                VERIFY(firstPropagation.has_value());
                return true;
            }

            newClause.clear();
            tryLearnIndex += 1;
            newClause.push_back(entry.literal);
            // TODO: Storing the 'includedInNewClause' guard in the opposite literal is not so nice
            infoFor(negate(entry.literal)).includedInNewClause = tryLearnIndex;
            seenSinglePropagatingReason = true;
        } else
            seenSinglePropagatingReason = false;

        VERIFY(!entry.reason.isDecision());

        auto [clause, forceLiteralIndex] = theoryFor(entry.reason).reasonToClause(*this, entry.literal, entry.reason);
        for (int_t index = 0; index < (int_t)clause.size(); index++) {
            Literal lit = clause[index];
            auto& info = infoFor(negate(lit));

            if (index != forceLiteralIndex)
                VERIFY(wasFalse(lit));
            else
                VERIFY(wasTrue(lit));

            if (info.tentativelyTrue()) {
                // TODO: Storing the 'includedInNewClause' guard in the opposite literal is not so nice
                if (info.includedInNewClause != tryLearnIndex) {
                    newClause.push_back(lit);
                    info.includedInNewClause = tryLearnIndex;
                }
            } else if (index != forceLiteralIndex) {
                auto it = std::find_if(subTrace.begin(), subTrace.end(), [l = negate(lit)](SubTraceEntry entry) { return entry.literal == l; });
                VERIFY(it != subTrace.end());
                VERIFY(it - subTrace.begin() < position);
                VERIFY(it - subTrace.begin() == (int_t)info.subTraceIndex);
                if (!shouldBeVisited[info.subTraceIndex]) {
                    openLiterals += 1;
                    shouldBeVisited[info.subTraceIndex] = true;
                }
            }
        }
        if (!theoryFor(entry.reason).isPropagating())
            seenSinglePropagatingReason = false;
    }
}

bool Solver::analyzeConflicts() {
    VERIFY(!conflicts.empty());

    auto doesConflictPersist = [this](Conflict conflict) {
        bool isReasonValid = testReason(conflict.literal, conflict.reason);
        bool isImpliedLiteralFalse = infoFor(negate(conflict.literal)).tentativelyTrue();
        return isReasonValid && isImpliedLiteralFalse;
    };

    for (;;) {
        Conflict drivingConflict = conflicts.back();

        auto removeResolvedConflcits = [&] {
            while (!conflicts.empty()) {
                if (doesConflictPersist(conflicts.back())) {
                    // Remember the last conflict that persisted
                    drivingConflict = conflicts.back();
                    return;
                }
                conflicts.pop_back();
            }
        };

        // Backtrack until all conflicts are resolved
        while (!conflicts.empty()) {
            if (currentDecisionLevel() == -1)
                return false;

            backtrack(currentDecisionLevel());
            removeResolvedConflcits();
        }

        // Learn from the last conflict that was resolved
        if (tryLearn(drivingConflict))
            return true;

        // tryLearn() detected that a conflict still persists, find it by propagating
        propagate();
        VERIFY(!conflicts.empty());
    }
}

void Solver::backtrack(int_t targetLevel) {
    VERIFY(targetLevel >= 0);
    TracePosition position = decisions[targetLevel];
    while ((int_t)decisions.size() > targetLevel)
        decisions.pop_back();
    VERIFY(currentDecisionLevel() == targetLevel - 1);

    for (auto& theory : reasonTheories)
        theory->backtrack(*this);

    subTrace.clear();
    subTrace.reserve(trace.size() - position.index);

    TracePosition writePosition = position;
    TracePosition traceEnd = TracePosition(trace.size());
    for (; position < traceEnd; position++) {
        const TraceEntry entry = at(position);
        auto& theory = theoryFor(entry.literal);
        auto& info = infoFor(entry.literal);
        VERIFY(info.firstReason.has_value() && info.lastReason.has_value());

        bool revert = entry.reason.isDecision() || !testReason(entry.literal, entry.reason);
        if (revert) {
            if (info.firstReason.value() == position) {
                // When the first reason is reverted we requeue the propagation.
                info.subTraceIndex = subTrace.size();
                subTrace.push_back({ entry.literal, entry.reason });
                if (assignedTrue(entry.literal)) {
                    theory.unapplyAssignment(*this, entry.literal);
                    m_clauses.unapplyAssignment(*this, entry.literal);
                    queuePropagation(entry.literal);
                }
            }
            if (entry.nextReason.has_value()) {
                // tell nextReason to update prevReason
                at(*entry.nextReason).prevReason = entry.prevReason;
            } else if (entry.prevReason.has_value()) {
                info.lastReason = entry.prevReason;
                at(*entry.prevReason).nextReason = std::nullopt;
            } else {
                // revert the literal
                info.firstReason = std::nullopt;
                info.lastReason = std::nullopt;

                removePropagation(entry.literal);
            }
        } else {
            if (info.firstReason.value() == position) {
                if (assignedTrue(entry.literal)) {
                    theory.reapplyAssignment(*this, entry.literal);
                    m_clauses.reapplyAssignment(*this, entry.literal);
                }
            }
            *(entry.prevReason.has_value() ? &at(*entry.prevReason).nextReason : &info.firstReason) = writePosition;
            *(entry.nextReason.has_value() ? &at(*entry.nextReason).prevReason : &info.lastReason) = writePosition;

            at(writePosition) = entry;
            writePosition += 1;
        }
    }
    trace.resize(writePosition.index);

    // checkInvariances();
}

std::vector<Reason> Solver::collectReasons(Literal trueLit) {
    const auto& info = infoFor(trueLit);
    VERIFY(info.tentativelyTrue());

    VERIFY(info.firstReason.has_value());
    VERIFY(info.lastReason.has_value());
    std::vector<Reason> result;
    TracePosition pos = info.firstReason.value();
    for (;;) {
        result.push_back(at(pos).reason);
        if (!at(pos).nextReason.has_value())
            break;
        pos = at(pos).nextReason.value();
    }
    return result;
}

void Solver::checkInvariances() {
    // check reason linked lists
    auto checkLiteral = [this](Value val) {
        Literal lit = { val };
        const auto& info = infoFor(lit);
        if (!info.tentativelyTrue())
            return;
        VERIFY(info.firstReason.has_value());
        VERIFY(info.lastReason.has_value());
        TracePosition pos = info.firstReason.value();
        VERIFY(!at(pos).prevReason.has_value());
        VERIFY(at(pos).literal == lit);
        // print("{} ({} .. {}): {}", formatValue(lit), info.firstReason->index, info.lastReason->index, pos.index);

        while (at(pos).nextReason.has_value()) {
            TracePosition newPos = at(pos).nextReason.value();
            VERIFY(newPos > pos);
            // print(" -> {}", newPos.index);
            VERIFY(at(newPos).literal == lit);
            VERIFY(at(newPos).prevReason.has_value());
            VERIFY(at(newPos).prevReason.value() == pos);
            pos = newPos;
        }
        VERIFY(pos == info.lastReason.value());
        // println("");
    };
    for (auto& theory : valueTheories) {
        auto* bTheory = dynamic_cast<BooleanTheory*>(theory.theory);
        if (bTheory != nullptr) {
            for (int_t valueId = 0; valueId < valueCount(*bTheory); valueId++) {
                checkLiteral(Value { (uint32_t)bTheory->theoryId(), (uint32_t)valueId });
            }
        }
    }

    // check decisions
    for (int_t level = 0; level < (int_t)decisions.size(); level++) {
        VERIFY(at(decisions[level]).reason.isDecision());
        VERIFY(at(decisions[level]).reason.decisionLevel() == level);
    }

    // check propagation linked list
    VERIFY(firstPropagation.has_value() == lastPropagation.has_value());
    if (firstPropagation.has_value()) {
        Literal current = firstPropagation.value();
        VERIFY(!infoFor(current).prevPropagation.has_value());
        while (infoFor(current).nextPropagation.has_value()) {
            Literal next = infoFor(current).nextPropagation.value();
            VERIFY(infoFor(next).prevPropagation == current);
            current = next;
        }
        VERIFY(current == lastPropagation.value());
    }

    m_clauses.checkInvariances(*this);
}

void Solver::Clauses::checkInvariances(Solver& solver) {
    // check clause masks
    VERIFY(clauses.size() == clauseMasks.size());
    for (int_t clauseIndex = 0; clauseIndex < (int_t)clauses.size(); clauseIndex++) {
        auto mask = clauseMasks[clauseIndex];
        const auto& clause = clauses[clauseIndex];
        // VERIFY((mask & (literalMask(clause.size()) - (clause_mask_t)1)) == mask);
        for (int_t index = 0; index < (int_t)clause.size(); index++) {
            bool bitSet = (mask & literalMask(index)) != 0;
            VERIFY(bitSet == !solver.assignedFalse(clause[index]));
        }
        if (std::popcount(mask) == 1) {
            int_t index = std::countr_zero(mask);
            Literal trueLit = clause[index];
            auto reasons = solver.collectReasons(trueLit);
            VERIFY(std::any_of(reasons.begin(), reasons.end(), [&](Reason reason) {
                if (reason.reasonTheory == theoryId()) {
                    auto inst = asInstance(reason);
                    if ((int_t)inst.clauseIndex == clauseIndex) {
                        VERIFY(mask == literalMask(inst.literalIndex));
                        return true;
                    }
                }
                return false;
            }));

            for (Literal lit : clause) {
                if (lit != trueLit) {
                    VERIFY(solver.tentativelyTrue(solver.negate(lit)));
                    // VERIFY(solver.infoFor(lit).firstReason.value() < pos);
                }
            }
        }
    }
}

bool Solver::checkAssignment() {
    for (const auto& clause : m_clauses.clauses) {
        bool foundTrue = false;
        std::optional<Literal> unassignedInternal;
        for (Literal lit : clause) {
            if (infoFor(lit).tentativelyTrue()) {
                foundTrue = true;
                break;
            }
            if (lit.theoryId == internalVariables.theoryId() && !infoFor(!lit).tentativelyTrue())
                unassignedInternal = lit;
        }
        if (!foundTrue) {
            if (!unassignedInternal.has_value())
                return false;
            decideTrue(unassignedInternal.value());
            propagate();
        }
    }
    return true;
}

}