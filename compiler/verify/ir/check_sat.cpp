#include <verify/backend/Solver.h>
#include <verify/ir/check.h>
#include <verify/language/Parser.h>

#include <gtest/gtest.h>

namespace verify::ir {

//! States propositions as a boolean satisfiability problem and searches for a model
struct SatEncoder {
    explicit SatEncoder(const Function& function)
        : m_function(function) { }

    //! States that 'prop' holds
    void state(Bool prop);

    //! Whether the stated propositions have no model
    bool unsatisfiable();

private:
    static bool isConnective(Bool prop) {
        return prop.kind() == ExprKind::And || prop.kind() == ExprKind::Or;
    }
    static bool isConjunction(Bool prop) {
        return isConnective(prop) && (prop.kind() == ExprKind::And) != prop.negated();
    }
    static bool literalValue(Bool prop) {
        VERIFY(prop.kind() == ExprKind::BooleanLiteral);
        VERIFY(prop.id() == 0);
        return (bool)prop.negated();
    }

    //! Hashes a proposition by the bits of its handle
    struct PropHash {
        size_t operator()(Bool prop) const {
            size_t hash = 0;
            hash_combine(hash, std::bit_cast<uint32_t>((Expr)prop));
            return hash;
        }
    };

    //! The literal standing for 'prop', defining it by clauses when it is a connective
    backend::Bool literal(Bool prop);

    //! Adds the clauses that make 'variable' equivalent to the connective 'prop'
    void defineConnective(Bool prop, backend::Bool variable);

    //! Hands 'clause' to the solver, which expects its literals to be distinct
    void addClause(std::vector<backend::Bool> clause);

    //! The operands of a connective, negated when the connective itself is negated
    std::vector<Bool> operandsOf(Bool prop) const;

    //! A variable that no clause has forced a value on yet
    std::optional<backend::Bool> findUnassignedVariable();

    const Function& m_function;
    std::unique_ptr<backend::Solver> m_solver = backend::Solver::make();
    //! The variable of every proposition that is not a connective, keyed without its negation
    std::unordered_map<Bool, backend::Bool, PropHash> m_variables;
};

/*!
Distinct operands may still stand for the same literal, either because a proposition appears
twice or because both of them are boolean literals. A repetition is dropped and a literal
next to its complement makes the clause hold for every assignment, so it states nothing.
*/
void SatEncoder::addClause(std::vector<backend::Bool> clause) {
    std::vector<backend::Bool> distinct;
    for (backend::Bool lit : clause) {
        bool repeated = false;
        for (backend::Bool other : distinct) {
            if (other == !lit)
                return;
            repeated = repeated || other == lit;
        }
        if (!repeated)
            distinct.push_back(lit);
    }
    m_solver->addClause(std::move(distinct));
}

std::vector<Bool> SatEncoder::operandsOf(Bool prop) const {
    VERIFY(isConnective(prop));
    ExprList list = prop.kind() == ExprKind::And
        ? m_function.getAnd(prop).operands
        : m_function.getOr(prop).operands;

    std::vector<Bool> operands;
    operands.reserve(m_function.view(list).size());
    for (Expr operand : m_function.view(list))
        operands.push_back(prop.negated() ? !Bool(operand) : Bool(operand));
    return operands;
}

/*!
A conjunction holds exactly when each of its operands holds, so it is split up instead of
becoming a clause of its own. Everything else states one clause: a disjunction the clause of
its operands, any other proposition the unit clause of its literal.
*/
void SatEncoder::state(Bool prop) {
    if (isConjunction(prop)) {
        for (Bool operand : operandsOf(prop))
            state(operand);
        return;
    }

    std::vector<backend::Bool> clause;
    if (isConnective(prop)) {
        for (Bool operand : operandsOf(prop))
            clause.push_back(literal(operand));
    } else {
        clause.push_back(literal(prop));
    }
    addClause(std::move(clause));
}

backend::Bool SatEncoder::literal(Bool prop) {
    // The builtin literals are the only propositions whose value is already known
    if (prop.kind() == ExprKind::BooleanLiteral)
        return literalValue(prop) ? backend::true_literal : backend::false_literal;

    // A proposition and its negation share a variable, which is why the negation is not
    // part of the key. The expressions below the connectives are uniqued as well, so
    // equal subformulas are defined once and share their variable too.
    Bool base = prop.baseValue();

    auto it = m_variables.find(base);
    if (it != m_variables.end())
        return prop.negated() ? !it->second : it->second;

    // The variable is recorded before the operands are visited, so that the recursion
    // below cannot invalidate an iterator that is still in use
    backend::Bool variable = m_solver->newAuxBooleanVariable();
    m_variables.emplace(base, variable);
    if (isConnective(base))
        defineConnective(base, variable);
    return prop.negated() ? !variable : variable;
}

/*!
The definition of a disjunction is '!v or op_1 or ... or op_n' together with 'v or !op_i'
for every operand. A conjunction is the negation of the disjunction of the negated operands,
so the same clauses describe it once 'v' and every operand are negated.
*/
void SatEncoder::defineConnective(Bool prop, backend::Bool variable) {
    bool conjunction = isConjunction(prop);
    backend::Bool head = conjunction ? !variable : variable;

    std::vector<backend::Bool> definition { !head };
    for (Bool operand : operandsOf(prop)) {
        backend::Bool lit = literal(operand);
        if (conjunction)
            lit = !lit;
        definition.push_back(lit);
        addClause({ head, !lit });
    }
    addClause(std::move(definition));
}

std::optional<backend::Bool> SatEncoder::findUnassignedVariable() {
    backend::Solver& solver = *m_solver;
    int_t count = solver.booleanCount(backend::TheoryId::AuxBooleanVariables);
    for (int_t i = 0; i < count; i++) {
        auto lit = backend::Bool(backend::TheoryId::AuxBooleanVariables, i * 2);
        if (!solver.assignedTrue(lit) && !solver.assignedFalse(lit))
            return lit;
    }
    return std::nullopt;
}

bool SatEncoder::unsatisfiable() {
    backend::Solver& solver = *m_solver;
    if (solver.hasConflicts() || !solver.propagate())
        return true;

    // Every variable of the problem belongs to the aux theory, so an assignment that leaves
    // none of them open is a model and the propositions are satisfiable
    for (;;) {
        auto lit = findUnassignedVariable();
        if (!lit.has_value())
            break;

        solver.decideTrue(lit.value());
        while (!solver.propagate()) {
            if (!solver.analyzeConflicts())
                return true;
        }
    }

    // TODO: Missing generic API for:
    // VERIFY(solver.clauses.checkAssignment(solver));
    return false;
}

bool checkSatProof(const Function& function, Bool prop, const SatProof& proof) {
    SatEncoder encoder(function);
    // The proposition holds when the clauses leave no way for it to be false
    encoder.state(!prop);
    for (Theorem clause : proof.clauses)
        encoder.state(function.prop(clause));
    return encoder.unsatisfiable();
}

//! The result of checking the function 'source' describes
static FunctionCheckReport checkSource(const char* source) {
    Function function = language::parse(source).function;
    FunctionCheckReport report = check(function);
    // A malformed function would make the result of the proof check meaningless
    VERIFY(report.malformedExpressions.empty() && report.malformedInstructions.empty());
    return report;
}

//! The theorems of 'source' whose proof does not establish their proposition
static std::vector<Theorem> invalidProofs(const char* source) {
    return checkSource(source).invalidProofs;
}

static std::vector<Theorem> theorems(std::initializer_list<uint32_t> ids) {
    std::vector<Theorem> result;
    for (uint32_t id : ids)
        result.emplace_back(id);
    return result;
}

TEST(VerifyIR, SatResolution) {
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $q by sat:
        clause !$p or $q by sorry
        clause $p by sorry
)")
            .empty());

    // Without the second clause nothing rules out that both of them are false
    EXPECT_EQ(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $q by sat:
        clause !$p or $q by sorry
)"),
        theorems({ 1 }));

    // Clauses that say nothing about the proposition do not establish it either
    EXPECT_EQ(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $q by sat:
        clause $p by sorry
        clause !$p or $p by sorry
)"),
        theorems({ 2 }));
}

TEST(VerifyIR, SatClausesAreTheorems) {
    // Writing a clause down states a theorem, so its own proof is checked next to the sat
    // proof that rests on it and not as part of it
    FunctionCheckReport report = checkSource(R"(
fn #test($p: bool, $q: bool):
    prove $q by sat:
        clause !$p or $q by eq_reflexive
        clause $p by sorry
)");
    EXPECT_EQ(report.invalidProofs, theorems({ 0 }));
    EXPECT_EQ(report.sorryTheorems, theorems({ 1 }));
}

TEST(VerifyIR, SatClausesPrecedeTheirTheorem) {
    // 'p or p' leaves no way for 'p' to be false, so the clause does establish the proposition
    {
        Function fn;
        Bool p(fn.addParameter(Sort::Bool));
        Theorem clause = fn.addTheorem(fn.addOr({ p, p }), CodePos(0), Proof::makeSorry());
        fn.addTheorem(p, CodePos(0), fn.addSat({ { clause } }));
        EXPECT_TRUE(check(fn).invalidProofs.empty());
    }

    // The same proof is rejected when the clause is only stated after the theorem it serves.
    // The text form cannot express this, but a proof resting on itself would look like it.
    {
        Function fn;
        Bool p(fn.addParameter(Sort::Bool));
        Theorem theorem = fn.addTheorem(p, CodePos(0), fn.addSat({ { Theorem(1) } }));
        EXPECT_EQ(fn.addTheorem(fn.addOr({ p, p }), CodePos(0), Proof::makeSorry()), Theorem(1));
        EXPECT_EQ(check(fn).invalidProofs, std::vector<Theorem> { theorem });
    }
}

TEST(VerifyIR, SatClauseOfStatedTheorem) {
    // A clause may name a theorem that was stated elsewhere instead of stating a new one
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove %p_holds: $p by sorry
    prove $q by sat:
        clause !$p or $q by sorry
        clause %p_holds
)")
            .empty());

    // The same theorem may serve as a clause of more than one proof
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool, $r: bool):
    prove %p_holds: $p by sorry
    prove $q by sat:
        clause !$p or $q by sorry
        clause %p_holds
    prove $r by sat:
        clause !$p or $r by sorry
        clause %p_holds
)")
            .empty());
}

TEST(VerifyIR, SatTautology) {
    // A proposition that cannot be false at all is established by any set of clauses
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $p or !$p by sat:
        clause $q by sorry
)")
            .empty());

    EXPECT_EQ(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $p or $p by sat:
        clause $q by sorry
)"),
        theorems({ 1 }));
}

TEST(VerifyIR, SatConnectives) {
    // The negated proposition is a disjunction, so both operands have to be forced
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $p and $q by sat:
        clause $p by sorry
        clause $q by sorry
)")
            .empty());

    EXPECT_EQ(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $p and $q by sat:
        clause $p by sorry
)"),
        theorems({ 1 }));

    // A negated 'and' clause states the disjunction of the negated operands
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove !$q by sat:
        clause !($p and $q) by sorry
        clause $p by sorry
)")
            .empty());

    // A conjunction below a disjunction is not a clause of its own and needs a variable
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool, $r: bool):
    prove $r by sat:
        clause ($p and $q) or $r by sorry
        clause !$p or !$q by sorry
)")
            .empty());

    // Without the second clause the conjunction can be true and 'r' false
    EXPECT_EQ(invalidProofs(R"(
fn #test($p: bool, $q: bool, $r: bool):
    prove $r by sat:
        clause ($p and $q) or $r by sorry
)"),
        theorems({ 1 }));
}

TEST(VerifyIR, SatRepeatedOperands) {
    // Operands standing for the same literal state it once, they are not a clause of two
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool):
    prove $p by sat:
        clause $p or $p by sorry
)")
            .empty());

    // A clause holding a literal next to its complement holds always and states nothing
    EXPECT_EQ(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $q by sat:
        clause $p or !$p by sorry
)"),
        theorems({ 1 }));
}

TEST(VerifyIR, SatBooleanLiterals) {
    EXPECT_TRUE(invalidProofs(R"(
fn #test($q: bool):
    prove true by sat:
        clause $q by sorry
)")
            .empty());

    // Everything follows from a false clause
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool):
    prove $p by sat:
        clause false by sorry
)")
            .empty());

    EXPECT_EQ(invalidProofs(R"(
fn #test():
    prove false by sat:
        clause true by sorry
)"),
        theorems({ 1 }));
}

TEST(VerifyIR, SatVariables) {
    // Propositions that are not connectives are opaque, so only the same expression resolves
    EXPECT_TRUE(invalidProofs(R"(
fn #test($a, $b, $c):
    prove $a = $c by sat:
        clause $a = $b or $a = $c by sorry
        clause $a != $b by sorry
)")
            .empty());

    // The order of the operands is part of the identity of an equality
    EXPECT_EQ(invalidProofs(R"(
fn #test($a, $b, $c):
    prove $a = $c by sat:
        clause $a = $b or $a = $c by sorry
        clause $b != $a by sorry
)"),
        theorems({ 2 }));
}

TEST(VerifyIR, SatProofOfActivity) {
    // The clauses of the phi tactics combine to the activity of a phi that was jumped to
    EXPECT_TRUE(invalidProofs(R"(
fn #test():
@jump:
    jump @phi
    nop
@phi:
    phi @jump
    nop
    prove !@phi.active or @jump.active by sat:
        clause @phi.from@jump or !@phi.active by phi_enumerate
        clause !@phi.from@jump or @jump.active by phi_active_source
)")
            .empty());
}

}