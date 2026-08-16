#include <verify/ir/check.h>

#include <algorithm>

namespace verify::ir {

// Proof specific checks, implemented in other .cpp files.
bool checkPatternTactic(const Function&, Bool prop, Tactic);
bool checkPhiEnumerate(const Function&, Bool prop);
bool checkSatProof(const Function&, Bool pop, const SatProof&);

//! Collects the results of all checks performed on a single function
struct FunctionChecker {
    explicit FunctionChecker(Function& function)
        : function(function) { }

    FunctionCheckReport check();

    //! Checks that the arguments of every expression have the declared sort
    void checkExpressionSorts();

    //! Checks that the arguments of every instruction have the declared sort
    void checkInstructionSorts();

    template<typename Data>
    bool argumentSortsMatch(const Data&);

    template<ExprKind kind, typename Data>
    void checkSortsOfKind(Expr, const Data&);

    //! Checks that the proof of every theorem establishes its proposition
    void checkProofs();

    //! Checks that the preconditions of every expression and instruction are proven by a theorem
    void checkPreconditions();

    Function& function;
    FunctionCheckReport report;
};

FunctionCheckReport FunctionChecker::check() {
    checkExpressionSorts();
    checkInstructionSorts();
    checkProofs();
    checkPreconditions();
    return std::move(report);
}

/*!
The sort an argument must have is the sort of the expression type it is declared with in
'expressions.inc' and 'instructions.inc'. Arguments that are declared as a plain 'Expr' accept
any sort and arguments that are not expressions at all have no sort to check.
*/
template<typename Data>
bool FunctionChecker::argumentSortsMatch(const Data& data) {
    bool valid = true;
    function_detail::forEachMember(data, [&](const auto& member) {
        using member_t = std::remove_cvref_t<decltype(member)>;
        if constexpr (requires { member_t::sort; }) {
            if (function.sortOf(member) != member_t::sort)
                valid = false;
        }
    });
    return valid;
}

template<ExprKind kind, typename Data>
void FunctionChecker::checkSortsOfKind(Expr expr, const Data& data) {
    if constexpr (kind == ExprKind::Equality) {
        // An equality accepts any sort as long as both of its sides agree
        if (function.sortOf(data.left) != function.sortOf(data.right))
            report.malformedExpressions.push_back(expr);
    } else if constexpr (variadicOperandSort(kind).has_value()) {
        static constexpr Sort operandSort = variadicOperandSort(kind).value();
        bool valid = data.operands.size() >= 2;
        for (Expr operand : function.view(data.operands)) {
            if (function.sortOf(operand) != operandSort)
                valid = false;
        }
        if (!valid)
            report.malformedExpressions.push_back(expr);
    } else if (!argumentSortsMatch(data)) {
        report.malformedExpressions.push_back(expr);
    }
}

void FunctionChecker::checkExpressionSorts() {
    for (Expr expr : function.expressions()) {
        switch (expr.kind()) {
            // Only compound expressions are enumerated, the inline ones carry no arguments
#define COMPOUND_EXPR(name, sortType, args...)                                      \
    case ExprKind::name:                                                            \
        checkSortsOfKind<ExprKind::name>(expr, function.get##name((sortType)expr)); \
        break;
#include <verify/ir/expressions.inc>
        default:
            VERIFY_NOT_REACHED();
        }
    }
}

void FunctionChecker::checkInstructionSorts() {
    for (uint32_t id = 0; id < function.here().id(); id++) {
        CodePos pos(id);
        bool valid = true;
        switch (function.opcodeAt(pos)) {
#define INSTRUCTION(name, args...)                           \
    case Opcode::name:                                       \
        valid = argumentSortsMatch(function.get##name(pos)); \
        break;
#include <verify/ir/instructions.inc>
        default:
            VERIFY_NOT_REACHED();
        }
        if (!valid)
            report.malformedInstructions.push_back(pos);
    }
}

void FunctionChecker::checkProofs() {
    for (Theorem theorem : function.theorems()) {
        switch (function.proof(theorem).tactic()) {
        case Tactic::Sorry:
            report.sorryTheorems.push_back(theorem);
            continue;
        case Tactic::SmtSolve:
            report.smtSolveTheorems.push_back(theorem);
            continue;
        case Tactic::Precondition:
            // A precondition is assumed, not proven
            continue;
        case Tactic::Sat: {
            const SatProof& proof = function.getSat(function.proof(theorem));
            // A clause has to be stated before the theorem it serves, so that the theorems can
            // be checked in the order they are listed in and no proof can rest on itself
            bool ordered = std::ranges::all_of(proof.clauses, [theorem](Theorem clause) { return clause.id() < theorem.id(); });
            if (!ordered || !checkSatProof(function, function.prop(theorem), proof))
                report.invalidProofs.push_back(theorem);
            continue;
        }
        case Tactic::PhiEnumerate:
            if (!checkPhiEnumerate(function, function.prop(theorem)))
                report.invalidProofs.push_back(theorem);
            continue;
        default:
            if (!checkPatternTactic(function, function.prop(theorem), function.proof(theorem).tactic()))
                report.invalidProofs.push_back(theorem);
            continue;
        }
    }
}

void FunctionChecker::checkPreconditions() {
    // Note: This loop may add more expressions which will not be checked themselves
    for (Expr expr : function.expressions()) {
        if (!isLoad(expr.kind()))
            continue;
        Type locationType = function.addMemoryLocType({ function.getLoad(expr).loc });
        Bool precondition = function.addScalarType({ locationType, function.sortOf(expr) });
        if (!function.findTheorem(precondition).has_value())
            report.invalidExpressions.push_back({ expr, precondition });
    }

    for (uint32_t id = 0; id < function.here().id(); id++) {
        CodePos pos(id);
        if (function.opcodeAt(pos) != Opcode::Store)
            continue;
        auto store = function.getStore(pos);
        Type locationType = function.addMemoryLocType({ store.loc });
        Bool precondition = function.addScalarType({ locationType, function.sortOf(store.value) });
        if (!function.findTheorem(precondition).has_value())
            report.invalidInstructions.push_back({ pos, precondition });
    }
}

FunctionCheckReport check(Function& function) {
    return FunctionChecker(function).check();
}

}
