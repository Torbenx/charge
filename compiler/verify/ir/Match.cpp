#include <verify/ir/Match.h>

#include <verify/language/Parser.h>

#include <gtest/gtest.h>

#include <bitset>

namespace verify::ir {

Pattern::Pattern(const char* source) {
    language::ParsedFunction parsed = language::parse(source);
    m_function = std::move(parsed.function);
    m_parameterNames = std::move(parsed.parameterNames);
    m_labels = std::move(parsed.labels);

    // Everything a pattern states besides its preconditions is the shape it matches
    std::optional<Theorem> stated;
    for (Theorem theorem : m_function.theorems()) {
        if (m_function.proof(theorem).tactic() != Tactic::Sorry)
            continue;
        VERIFY(!stated.has_value());
        stated = theorem;
    }
    VERIFY(stated.has_value());
    m_prop = m_function.prop(*stated);

    // A 'nop' separates the positions around it, every other instruction ties them together
    uint32_t end = m_function.here().id();
    uint32_t run = 0;
    uint32_t offset = 0;
    m_positions.reserve(end + 1);
    for (uint32_t id = 0; id <= end; id++) {
        m_positions.push_back({ .run = run, .offset = offset });
        if (id < end && m_function.opcodeAt(CodePos(id)) == Opcode::Nop) {
            run += 1;
            offset = 0;
        } else {
            offset += 1;
        }
    }
    m_runCount = run + 1;
}

Placeholder Pattern::placeholder(std::string_view name) const {
    for (int_t i = 0; i < (int_t)m_parameterNames.size(); i++) {
        if (m_parameterNames[i] == name)
            return Placeholder(i);
    }
    VERIFY_NOT_REACHED(); // The pattern has no such parameter
}

LabelPlaceholder Pattern::label(std::string_view name) const {
    for (const auto& [labelName, pos] : m_labels) {
        if (labelName == name)
            return LabelPlaceholder(pos.id());
    }
    VERIFY_NOT_REACHED(); // The pattern has no such label
}

Match::Match(const Pattern& pattern)
    : m_pattern(pattern)
    , m_placeholders(pattern.function().parameterCount())
    , m_runBase(pattern.runCount())
    , m_edges(pattern.function().controlFlowEdgeCount()) { }

std::optional<CodePos> Match::position(CodePos patternPos) const {
    const Pattern::PositionInfo& info = m_pattern.positionInfo(patternPos);
    std::optional<CodePos> base = m_runBase[info.run];
    if (!base.has_value())
        return std::nullopt;
    return CodePos(base->id() + info.offset);
}

struct Matcher {
    Matcher(const Pattern& pattern, const Function& target)
        : pattern(pattern), target(target), result(pattern) { }

    bool matchClause(Bool patternProp, Bool prop);
    bool matchExpr(Expr patternExpr, Expr targetExpr);
    bool matchInstructions();

    //! Matches the arguments of two expressions or instructions of the same kind pairwise
    template<typename Data>
    bool matchData(const Data& patternData, const Data& targetData);

    template<typename T>
    bool matchMember(const T& patternMember, const T& targetMember);

    bool matchInstruction(CodePos patternPos, CodePos targetPos);
    bool checkPhi(ControlFlowEdgeList patternEdges, ControlFlowEdgeList targetEdges);
    bool matchList(ExprList patternList, ExprList targetList);
    bool matchOperands(ExprList patternOperands, ExprList targetOperands);
    bool matchRelativePosition(RelativeCodePos patternPos, RelativeCodePos targetPos);

    bool bindPlaceholder(Expr patternExpr, Expr targetExpr);
    bool bindPosition(CodePos patternPos, CodePos targetPos);
    bool bindSort(Sort patternSort, Sort targetSort);
    bool bindEdge(ControlFlowEdge patternEdge, ControlFlowEdge targetEdge);

    const Pattern& pattern;
    const Function& target;
    Match result;
};

bool Matcher::matchExpr(Expr patternExpr, Expr targetExpr) {
    if (patternExpr.kind() == ExprKind::FunctionParameter)
        return bindPlaceholder(patternExpr, targetExpr);

    // Loads of different sorts only differ in the kind they are declared with, and the sort of a
    // pattern is a placeholder, so they are compared without it
    if (isLoad(patternExpr.kind()) && isLoad(targetExpr.kind())) {
        if (patternExpr.boolNegatedBit != targetExpr.boolNegatedBit)
            return false;
        if (!bindSort(pattern.function().sortOf(patternExpr), target.sortOf(targetExpr)))
            return false;
        Function::LoadData patternLoad = pattern.function().getLoad(patternExpr);
        Function::LoadData targetLoad = target.getLoad(targetExpr);
        return matchExpr(patternLoad.loc, targetLoad.loc)
            && matchRelativePosition(patternLoad.pos, targetLoad.pos);
    }

    if (patternExpr.kind() != targetExpr.kind())
        return false;
    if (patternExpr.boolNegatedBit != targetExpr.boolNegatedBit)
        return false;

    switch (patternExpr.kind()) {
    case ExprKind::PositionActive:
        return bindPosition(CodePos(patternExpr.id()), CodePos(targetExpr.id()));
    case ExprKind::ControlFlowEdgeTaken:
        return bindEdge(ControlFlowEdge(patternExpr.id()), ControlFlowEdge(targetExpr.id()));

        // Only compound expressions carry arguments of their own
#define COMPOUND_EXPR(name, sortType, args...)                                \
    case ExprKind::name:                                                      \
        return matchData(pattern.function().get##name((sortType)patternExpr), \
            target.get##name((sortType)targetExpr));
#include <verify/ir/expressions.inc>

    default:
        // The remaining expressions carry their whole argument in the handle
        return patternExpr.id() == targetExpr.id();
    }
}

template<typename Data>
bool Matcher::matchData(const Data& patternData, const Data& targetData) {
    bool valid = true;
    function_detail::forEachMemberPair(patternData, targetData,
        [&](const auto& patternMember, const auto& targetMember) {
            if (valid)
                valid = matchMember(patternMember, targetMember);
        });
    return valid;
}

template<typename T>
bool Matcher::matchMember(const T& patternMember, const T& targetMember) {
    if constexpr (std::same_as<T, CodePos>) {
        return bindPosition(patternMember, targetMember);
    } else if constexpr (std::same_as<T, RelativeCodePos>) {
        return matchRelativePosition(patternMember, targetMember);
    } else if constexpr (std::same_as<T, ControlFlowEdgeList>) {
        // The only instruction with ControlFlowEdgeList as a member is a phi.
        // Use this to check the structural correctness of the bindings.
        return checkPhi(patternMember, targetMember);
    } else if constexpr (std::same_as<T, ExprList>) {
        return matchList(patternMember, targetMember);
    } else if constexpr (std::same_as<T, Sort>) {
        return bindSort(patternMember, targetMember);
    } else if constexpr (std::derived_from<T, Expr>) {
        return matchExpr(patternMember, targetMember);
    } else if constexpr (function_detail::is_padding<T>) {
        return true;
    } else {
        static_assert(std::derived_from<T, SmallHandle>);
        return patternMember == targetMember;
    }
}

/*!
A clause stays true when further operands are added to it, so the proposition may have more of
them than the pattern as long as it ends in the shape the pattern states. That only holds for
the clause itself: an operand of it that is a connective again would state something stronger
with further operands, so those are matched as a whole.
*/
bool Matcher::matchClause(Bool patternProp, Bool prop) {
    if (patternProp.kind() != ExprKind::Or || patternProp.negated())
        return matchExpr(patternProp, prop);
    if (prop.kind() != ExprKind::Or || prop.negated())
        return false;
    return matchOperands(pattern.function().getOr(patternProp).operands,
        target.getOr(prop).operands);
}

bool Matcher::matchOperands(ExprList patternOperands, ExprList targetOperands) {
    std::span<const Expr> patternOps = pattern.function().view(patternOperands);
    std::span<const Expr> targetOps = target.view(targetOperands);
    if (targetOps.size() < patternOps.size())
        return false;
    targetOps = targetOps.subspan(targetOps.size() - patternOps.size());
    for (size_t i = 0; i < patternOps.size(); i++) {
        if (!matchExpr(patternOps[i], targetOps[i]))
            return false;
    }
    return true;
}

bool Matcher::matchList(ExprList patternList, ExprList targetList) {
    std::span<const Expr> patternExprs = pattern.function().view(patternList);
    std::span<const Expr> targetExprs = target.view(targetList);
    if (patternExprs.size() != targetExprs.size())
        return false;
    for (size_t i = 0; i < patternExprs.size(); i++) {
        if (!matchExpr(patternExprs[i], targetExprs[i]))
            return false;
    }
    return true;
}

bool Matcher::matchRelativePosition(RelativeCodePos patternPos, RelativeCodePos targetPos) {
    // TODO: Relative positions
    if (!patternPos.simple() || !targetPos.simple())
        return false;
    return bindPosition(CodePos(patternPos.offset()), CodePos(targetPos.offset()));
}

bool Matcher::checkPhi(ControlFlowEdgeList patternEdges, ControlFlowEdgeList targetEdges) {
    // TODO: Some of this may be invariances of the match algorithm rather than actual checks.
    for (int_t i = 0; i < patternEdges.size(); i++) {
        ControlFlowEdge patternEdge = patternEdges.at(i);
        if (patternEdge.id() < result.m_edges.size() && result.m_edges[patternEdge.id()].has_value()) {
            if (!targetEdges.contains(*result.m_edges[patternEdge.id()]))
                return false;
            continue;
        }

        std::optional<CodePos> sourcePos = result.position(pattern.function().edgeSource(patternEdge));
        if (!sourcePos.has_value())
            continue; // Nothing determined where this edge comes from

        bool found = false;
        for (int_t j = 0; j < targetEdges.size(); j++)
            found |= target.edgeSource(targetEdges.at(j)) == *sourcePos;
        if (!found)
            return false;
    }
    return true;
}

bool Matcher::bindPlaceholder(Expr patternExpr, Expr targetExpr) {
    // A negated placeholder stands for the negation of what it is matched to
    Expr value = targetExpr;
    value.boolNegatedBit ^= patternExpr.boolNegatedBit;

    std::optional<Expr>& binding = result.m_placeholders[patternExpr.id()];
    if (binding.has_value())
        return *binding == value;
    binding = value;
    return true;
}

//! Matching one position of a run fixes every other position of it
bool Matcher::bindPosition(CodePos patternPos, CodePos targetPos) {
    const Pattern::PositionInfo& info = pattern.positionInfo(patternPos);
    if (targetPos.id() < info.offset)
        return false; // The run would start before the beginning of the function
    CodePos base(targetPos.id() - info.offset);

    VERIFY(info.run < result.m_runBase.size());
    std::optional<CodePos>& binding = result.m_runBase[info.run];
    if (binding.has_value())
        return *binding == base;
    binding = base;
    return true;
}

bool Matcher::bindSort(Sort patternSort, Sort targetSort) {
    VERIFY(patternSort == Sort::UninterpretedConstant);
    if (result.m_uninterpretedSort.has_value())
        return *result.m_uninterpretedSort == targetSort;
    result.m_uninterpretedSort = targetSort;
    return true;
}

bool Matcher::bindEdge(ControlFlowEdge patternEdge, ControlFlowEdge targetEdge) {
    if (!bindPosition(pattern.function().edgeTarget(patternEdge), target.edgeTarget(targetEdge)))
        return false;
    if (!bindPosition(pattern.function().edgeSource(patternEdge), target.edgeSource(targetEdge)))
        return false;

    VERIFY(patternEdge.id() < result.m_edges.size());
    std::optional<ControlFlowEdge>& binding = result.m_edges[patternEdge.id()];
    if (binding.has_value())
        return *binding == targetEdge;

    // Different edges in the pattern must bind to different edges in the target
    for (const std::optional<ControlFlowEdge>& other : result.m_edges) {
        if (other.has_value() && *other == targetEdge)
            return false;
    }
    binding = targetEdge;
    return true;
}

bool Matcher::matchInstruction(CodePos patternPos, CodePos targetPos) {
    if (targetPos.id() >= target.here().id())
        return false;
    Opcode opcode = pattern.function().opcodeAt(patternPos);
    if (target.opcodeAt(targetPos) != opcode)
        return false;

    switch (opcode) {
#define INSTRUCTION(name, args...)                                 \
    case Opcode::name:                                             \
        return matchData(pattern.function().get##name(patternPos), \
            target.get##name(targetPos));
#include <verify/ir/instructions.inc>
    default:
        VERIFY_NOT_REACHED();
    }
}

bool Matcher::matchInstructions() {
    int_t instructionCount = pattern.function().here().id();
    // Limit instruction count to 64 for efficiency.
    // Since the excerised maximum is currently 4 there is plenty of buffer.
    VERIFY(instructionCount < 64);
    std::bitset<64> pending;
    for (int_t instIndex = 0; instIndex < instructionCount; instIndex++) {
        // A 'nop' only separates positions, it does not stand for an instruction
        if (pattern.function().opcodeAt(CodePos(instIndex)) != Opcode::Nop)
            pending[instIndex] = true;
    }

    // Matching an instruction can determine the positions of further ones
    while(pending.any()) {
        bool madeProgress = false;
        for (int_t instIndex = 0; instIndex < instructionCount; instIndex++) {
            if (!pending.test(instIndex))
                continue;
            CodePos patternPos(instIndex);
            std::optional<CodePos> targetPos = result.position(patternPos);
            if (!targetPos.has_value())
                continue; // Nothing determined where this instruction has to be yet

            pending.reset(instIndex);
            madeProgress = true;
            if (!matchInstruction(patternPos, *targetPos))
                return false;
        }
        // Not all instructions where matched. This is most likely a structure issue with the pattern.
        if (!madeProgress && pending.any())
            return false;
    }

    return true;
}

std::optional<Match> matchClause(const Pattern& pattern, const Function& function, Bool prop) {
    Matcher matcher(pattern, function);
    if (!matcher.matchClause(pattern.prop(), prop))
        return std::nullopt;
    if (!matcher.matchInstructions())
        return std::nullopt;
    return std::move(matcher.result);
}

TEST(VerifyIRTactics, NestedConnective) {
    Pattern pattern(R"(
fn #_($a, $b, $c):
    prove $a = $a or !($b = $b or $c = $c) by sorry
)");
    Function function = language::parse(R"(
fn #test($x, $y, $z, $w):
    prove $w = $w or $x = $x or !($y = $y or $z = $z) by sorry
    prove $x = $x or !($w = $w or $y = $y or $z = $z) by sorry
)")
                            .function;

    // Only the clause itself may have further operands, an operand of it may not
    EXPECT_TRUE(matchClause(pattern, function, function.prop(Theorem(0))).has_value());
    EXPECT_FALSE(matchClause(pattern, function, function.prop(Theorem(1))).has_value());
}

TEST(VerifyIR, MatchUnplacedInstruction) {
    // Nothing in the clause says where the store of this pattern would have to be
    Pattern pattern(R"(
fn #_($a):
    nop
@store:
    store $a <- $a
@end:
    prove $a = $a by sorry
)");
    Function function = language::parse(R"(
fn #test($x):
    store $x <- $x
    prove $x = $x by sorry
)")
                            .function;
    EXPECT_FALSE(matchClause(pattern, function, function.prop(Theorem(0))).has_value());
}

TEST(VerifyIR, MatchInstructionsMatchesMoreInstructions) {
    Pattern pattern(R"(
fn #_($a, $b: uninterpreted_constant):
    pre %scalar: $a.type.uninterpreted_constant_scalar
    store $a <- $b
@after_store:
    nop
    store $a <- $a.load@after_store
@end:
    prove $a.load@end = $b by sorry
)");
    Function function = language::parse(R"(
fn #test($x):
    pre %scalar: $x.type.bool_scalar
    store $x <- true
@after_store1:
    store $x <- $x.load@after_store1
@after_store2:
    prove $x.load@after_store2 = true by sorry
)")
                            .function;
    EXPECT_TRUE(matchClause(pattern, function, function.prop(Theorem(1))).has_value());
}

TEST(VerifyIR, MatchedValues) {
    Pattern pattern(R"(
fn #_($x, $target, $value):
    pre %scalar: $x.type.uninterpreted_constant_scalar
@store:
    store $target <- $value
@after:
    prove $x != $target or $x.load@after = $value by sorry
)");
    Function function = language::parse(R"(
fn #test($a, $b, $c: memory_decl):
    pre %scalar: $a.type.memory_decl_scalar
    nop
    store $b <- $c
@after:
    prove $a != $b or $a.load@after = $c by load_store
)")
                            .function;

    std::optional<Match> result = matchClause(pattern, function, function.prop(Theorem(1)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)[pattern.placeholder("x")], Expr(ExprKind::FunctionParameter, 0));
    EXPECT_EQ((*result)[pattern.placeholder("target")], Expr(ExprKind::FunctionParameter, 1));
    EXPECT_EQ((*result)[pattern.placeholder("value")], Expr(ExprKind::FunctionParameter, 2));
    EXPECT_EQ((*result)[pattern.label("after")], CodePos(2));

    // The store the clause is about is found although the proposition never names its position
    EXPECT_EQ((*result)[pattern.label("store")], CodePos(1));

    // The sort a pattern is written with is a placeholder like its parameters are
    EXPECT_EQ(result->uninterpretedSort(), Sort::MemoryDecl);
}

}
