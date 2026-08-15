#include <verify/ir/Sat.h>

#include <verify/backend/SolverImpl.h>

#include <unordered_map>

namespace verify::ir {

namespace {

    bool isConnective(Bool prop) {
        return prop.kind() == ExprKind::And || prop.kind() == ExprKind::Or;
    }

    //! Whether 'prop' holds exactly when all of its operands hold
    /*!
    Negation is part of the proposition, so an 'or' below a negation is a conjunction of the
    negated operands and the other way round, as De Morgan's laws state.
    */
    bool isConjunction(Bool prop) {
        return isConnective(prop) && (prop.kind() == ExprKind::And) != (bool)prop.boolNegatedBit;
    }

    //! The value a boolean literal denotes
    /*!
    Both the id and the negation bit carry the value, so that 'true' can be written either as
    the literal itself or as the negation of 'false'.
    */
    bool literalValue(Bool prop) {
        VERIFY(prop.kind() == ExprKind::BooleanLiteral);
        return (prop.id() != 0) != (bool)prop.boolNegatedBit;
    }

    //! Hashes a proposition by the bits of its handle
    /*!
    Propositions are uniqued by the function, so equal expressions have equal handles and the
    handle is all that has to be compared.
    */
    struct PropHash {
        size_t operator()(Bool prop) const {
            size_t hash = 0;
            hash_combine(hash, std::bit_cast<uint32_t>((Expr)prop));
            return hash;
        }
    };

    //! States propositions as a boolean satisfiability problem and searches for a model
    struct SatEncoder {
        explicit SatEncoder(const Function& function)
            : m_function(function) { }

        //! States that 'prop' holds
        void state(Bool prop);

        //! Whether the stated propositions have no model
        bool unsatisfiable();

    private:
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
        backend::SolverImpl m_solver;
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
        m_solver.addClause(std::move(distinct));
    }

    std::vector<Bool> SatEncoder::operandsOf(Bool prop) const {
        VERIFY(isConnective(prop));
        ExprList list = prop.kind() == ExprKind::And
            ? m_function.getAnd(prop).operands
            : m_function.getOr(prop).operands;

        std::vector<Bool> operands;
        operands.reserve(m_function.view(list).size());
        for (Expr operand : m_function.view(list))
            operands.push_back(prop.boolNegatedBit ? !Bool(operand) : Bool(operand));
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
        bool negated = prop.boolNegatedBit;
        Bool base = negated ? !prop : prop;

        auto it = m_variables.find(base);
        if (it != m_variables.end())
            return negated ? !it->second : it->second;

        // The variable is recorded before the operands are visited, so that the recursion
        // below cannot invalidate an iterator that is still in use
        backend::Bool variable = m_solver.newAuxBooleanVariable();
        m_variables.emplace(base, variable);
        if (isConnective(base))
            defineConnective(base, variable);
        return negated ? !variable : variable;
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
        int_t count = m_solver.booleanCount(backend::TheoryId::AuxBooleanVariables);
        for (int_t i = 0; i < count; i++) {
            auto lit = backend::Bool(backend::TheoryId::AuxBooleanVariables, i * 2);
            if (!m_solver.assignedTrue(lit) && !m_solver.assignedFalse(lit))
                return lit;
        }
        return std::nullopt;
    }

    bool SatEncoder::unsatisfiable() {
        if (m_solver.hasConflicts() || !m_solver.propagate())
            return true;

        // Every variable of the problem belongs to the aux theory, so an assignment that leaves
        // none of them open is a model and the propositions are satisfiable
        for (;;) {
            auto lit = findUnassignedVariable();
            if (!lit.has_value())
                break;

            m_solver.decideTrue(lit.value());
            while (!m_solver.propagate()) {
                if (!m_solver.analyzeConflicts())
                    return true;
            }
        }

        VERIFY(m_solver.clauses.checkAssignment(m_solver));
        return false;
    }

}

bool provesPropBySat(const Function& function, const SatProof& proof, Bool prop) {
    SatEncoder encoder(function);
    // The proposition holds when the clauses leave no way for it to be false
    encoder.state(!prop);
    for (Theorem clause : proof.clauses)
        encoder.state(function.prop(clause));
    return encoder.unsatisfiable();
}

}
