#include <check/MemoryLocations.h>

#include <check/SatSolver.h>

#include <ReverseMemberPointer.h>

namespace check {

/*MemoryLocations* MemoryLocationEquality::kindTheory() {
    return ReverseMemberPointer<&MemoryLocations::m_equality>::reverse(this);
}

void MemoryLocationEquality::onNewVariable(Solver& solver, int_t eqId) {
    Link link = equalities.at(eqId);
    MemoryLocation source { link.source };
    MemoryLocation target { link.target };

    auto sourceRep = kindTheory()->normalRepresentation(solver, source);
    auto targetRep = kindTheory()->normalRepresentation(solver, target);

    if (sourceRep.expr.empty() && targetRep.expr.empty()) {
        StandardEquality::onNewVariable(solver, eqId);
        return;
    }

    Type sourceBaseType = solver.typeAtLocation(sourceRep.base);
    Type targetBaseType = solver.typeAtLocation(targetRep.base);

    BooleanValue typesEqual = solver.equality(sourceBaseType, targetBaseType);
    BooleanValue sourceHasTarget = solver.containsTypeLiteral(sourceBaseType, targetBaseType);
    BooleanValue targetHasSource = solver.containsTypeLiteral(targetBaseType, sourceBaseType);

    std::array<std::array<std::optional<BooleanValue>, 3>, 3> impliedByEquality;
    int_t equalityCaseCount = 0;

    // Encoding (13 clauses):
    //
    // (s == t) !(sType contains tType) (sBase.r1 != tBase) (sExpr != r1.tExpr)
    // (s == t) !(tType contains sType) (sBase != tBase.r2) (sExpr.r2 != tExpr)
    // (s == t) (sType != tType) (sBase != tBase) (sExpr != tExpr)
    //
    // (s != t) g1 g2 g3
    //
    // !g1 (sType constains tType)
    // !g1 (sBase.r1 == tBase)
    // !g1 (sExpr == r1.tExpr)
    //
    // !g2 (tType contains sType)
    // !g2 (sBase == tBase.r2)
    // !g2 (sExpr.r2 == tExpr)
    //
    // !g3 (sType == tType)
    // !g3 (sBase == tBase)
    // !g3 (sExpr == tExpr)


    auto addClauses = [&solver](BooleanValue negatedPremise, std::span<const std::optional<BooleanValue>, 3> consequences) {
        for (auto consequence : consequences) {
            if (consequence.has_value())
                solver.addClause({ negatedPremise, consequence.value() });
        }
    };

    VERIFY(equalityCaseCount != 0);
    if (equalityCaseCount == 1) {
        addClauses(negativeLiteral(eqId), impliedByEquality[0]);
    } else {
        std::vector<BooleanValue> glueClause;
        glueClause.reserve(equalityCaseCount + 1);
        glueClause.push_back(negativeLiteral(eqId));
        for (int_t i = 0; i < equalityCaseCount; i++) {
            int_t glueVarId = glueVariables.newVariable();
            glueClause.push_back(glueVariables.positiveLiteral(glueVarId));
            addClauses(glueVariables.negativeLiteral(glueVarId), impliedByEquality[i]);
        }
        solver.addClause(std::move(glueClause));
    }

    std::vector<std::vector<BooleanValue>> clauses;
    MemberExpression sourceExpr;
}

BooleanValue MemoryLocations::equality(Solver& solver, Value va, Value vb) {
    MemoryLocation a { va };
    MemoryLocation b { vb };

    if (a == b)
        return builtins::true_literal;

    auto aRep = normalRepresentation(solver, a);
    auto bRep = normalRepresentation(solver, b);

    auto aExpr = std::views::reverse(aRep.expr);
    auto bExpr = std::views::reverse(bRep.expr);
    auto [aIt, bIt] = std::ranges::mismatch(aExpr, bExpr);
    if (aIt != aExpr.begin()) {
        aRep.expr = { aRep.expr.begin(), std::prev(aIt).base() };
        bRep.expr = { bRep.expr.begin(), std::prev(bIt).base() };
        a = fromNormalRepresentation(solver, aRep);
        b = fromNormalRepresentation(solver, bRep);
    }

    return m_equality.equality(solver, a, b);
}*/

}