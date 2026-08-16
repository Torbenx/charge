#include <verify/language/Formatter.h>

#include <format>
#include <unordered_set>

#include <gtest/gtest.h>

namespace verify::language {

namespace {

    //! The indentation of one level, everything but a label line sits on at least one
    constexpr int_t INDENT = 4;
    //! The column a statement is broken into continuation lines at
    constexpr int_t LINE_LIMIT = 100;

    std::string_view tacticName(ir::Tactic tactic) {
        switch (tactic) {
#define TACTIC(name, snake_case) \
    case ir::Tactic::name:       \
        return #snake_case;
#include <verify/ir/tactics.inc>
        default:
            VERIFY_NOT_REACHED();
        }
    }

    std::string_view sortName(ir::Sort sort) {
        switch (sort) {
#define SORT(name, snake_case) \
    case ir::Sort::name:       \
        return #snake_case;
#include <verify/ir/sorts.inc>
        default:
            VERIFY_NOT_REACHED();
        }
    }

    //! Generates names as the lowest number that is not taken yet
    struct NameGenerator {
        explicit NameGenerator(std::span<const std::string> taken) {
            for (const std::string& name : taken) {
                if (!name.empty())
                    m_taken.insert(name);
            }
        }

        std::string next() {
            for (;;) {
                std::string name = std::to_string(m_next);
                m_next += 1;
                if (m_taken.insert(name).second)
                    return name;
            }
        }

    private:
        int_t m_next = 0;
        std::unordered_set<std::string> m_taken;
    };

    //! The levels of the expression grammar, from the weakest to the strongest binding one
    enum class Precedence : uint8_t {
        Or,
        And,
        Equality,
        Unary,
        Postfix,
    };
}

struct Formatter {
    explicit Formatter(const ParsedFunction& parsed)
        : parsedNames(parsed)
        , ir(parsed.function)
        , labels(parsed.labels)
        , theoremNames(parsed.theoremNames)
        , parameterNames(parsed.parameterNames) {
        // A caller that built the function itself may not have a name for everything
        labels.resize(positionCount());
        theoremNames.resize(ir.theoremCount());
        parameterNames.resize(ir.parameterCount());
    }

    std::string run() {
        planInlining();
        collectReferences();
        assignNames();
        emitFunction();
        return std::move(output);
    }

private:
    void planInlining() {
        theoremMentions.resize(ir.theoremCount(), 0);
        theoremHidden.resize(ir.theoremCount(), false);
        postCondition.resize(ir.theoremCount(), false);
        for (ir::Theorem theorem : ir.postConditions())
            postCondition[theorem.id()] = true;

        for (ir::Theorem theorem : ir.theorems()) {
            for (ir::Theorem clause : clausesOf(theorem))
                theoremMentions[clause.id()] += 1;
        }
        for (ir::Theorem theorem : ir.theorems()) {
            for (ir::Theorem clause : clausesOf(theorem))
                theoremHidden[clause.id()] = mayInline(clause, theorem);
        }
    }

    std::span<const ir::Theorem> clausesOf(ir::Theorem theorem) const {
        ir::Proof proof = ir.proof(theorem);
        if (proof.tactic() != ir::Tactic::Sat)
            return {};
        return ir.getSat(proof).clauses;
    }

    //! Whether a clause is written inside a sat tactic
    bool mayInline(ir::Theorem clause, ir::Theorem provenBy) const {
        return theoremNames[clause.id()].empty()
            && theoremMentions[clause.id()] == 1
            && ir.position(clause) == ir.position(provenBy)
            && ir.proof(clause).tactic() != ir::Tactic::Precondition
            && !postCondition[clause.id()];
    }

    void collectReferences() {
        referencedPositions.resize(positionCount(), false);
        visitedCompoundExpressions.resize(ir.expressionCount(), false);

        for (uint32_t id = 0; id < (uint32_t)ir.here().id(); id++)
            visitInstructions(ir::CodePos(id));
        for (ir::Theorem theorem : ir.theorems())
            visitExpr(ir.prop(theorem));
    }

    void visitStructMembers(const auto& s) {
        ir::function_detail::forEachMember(s, [&]<typename T>(T member) {
            if constexpr (std::same_as<T, ir::Expr>) {
                visitExpr(member);
            } else if constexpr (std::same_as<T, ir::ExprList>) {
                for (ir::Expr e : ir.view(member))
                    visitExpr(e);
            } else if constexpr (std::same_as<T, ir::CodePos>) {
                markReferenced(member);
            } else if constexpr (std::same_as<T, ir::RelativeCodePos>) { /* TODO: Implement formatting for relative code positions*/
                VERIFY(member.simple());
                markReferenced(member.simplePos());
            }
        });
    }

    void visitInstructions(ir::CodePos pos) {
        switch (ir.opcodeAt(pos)) {
#define INSTRUCTION(name, args...)             \
    case ir::Opcode::name:                     \
        visitStructMembers(ir.get##name(pos)); \
        break;
#include <verify/ir/instructions.inc>
        default:
            VERIFY_NOT_REACHED();
        }
    }

    void visitExpr(ir::Expr expr) {
        switch (expr.kind()) {
        case ir::ExprKind::PositionActive:
            markReferenced(ir::CodePos(expr.id()));
            break;
        case ir::ExprKind::ControlFlowEdgeTaken: {
            ir::ControlFlowEdge edge(expr.id());
            markReferenced(ir.edgeSource(edge));
            markReferenced(ir.edgeTarget(edge));
            break;
        }
        default:
            break;
        }

        if (!ir::isCompoundExpr(expr.kind()) || visitedCompoundExpressions[expr.id()])
            return;
        visitedCompoundExpressions[expr.id()] = true;

        switch (expr.kind()) {
#define COMPOUND_EXPR(name, sort, args...)                \
    case ir::ExprKind::name:                              \
        visitStructMembers(ir.get##name((ir::sort)expr)); \
        break;
#include <verify/ir/expressions.inc>

        default:
            VERIFY_NOT_REACHED();
        }
    }

    void markReferenced(ir::CodePos pos) {
        VERIFY(pos.id() < (uint32_t)positionCount());
        referencedPositions[pos.id()] = true;
    }

    //! Every name the source wrote down is kept, even where nothing refers to it
    void assignNames() {
        NameGenerator generator(labels);
        for (int_t id = 0; id < positionCount(); id++) {
            if (labels[id].empty() && referencedPositions[id])
                labels[id] = generator.next();
        }
        NameGenerator theorems(theoremNames);
        for (int_t id = 0; id < ir.theoremCount(); id++) {
            if (theoremNames[id].empty() && theoremMentions[id] > 0 && !theoremHidden[id])
                theoremNames[id] = theorems.next();
        }
        NameGenerator parameters(parameterNames);
        for (std::string& name : parameterNames) {
            if (name.empty())
                name = parameters.next();
        }
    }

    void emitFunction() {
        output += std::format("fn #{}(", parsedNames.name);
        for (int_t id = 0; id < (int_t)parameterNames.size(); id++) {
            if (id > 0)
                output += ", ";
            output += std::format("${}", parameterNames[id]);
            // A parameter written without a sort is a memory location
            ir::Sort sort = ir.sortOf(ir::Expr(ir::ExprKind::FunctionParameter, id));
            if (sort != ir::Sort::MemoryLoc)
                output += std::format(": {}", sortName(sort));
        }
        output += "):\n";

        // A theorem states what holds at its position before the instruction there runs, which
        // is the order the parser reads them back in
        std::vector<std::vector<ir::Theorem>> theoremsAt(positionCount());
        for (ir::Theorem theorem : ir.theorems())
            theoremsAt[ir.position(theorem).id()].push_back(theorem);

        for (int_t id = 0; id < positionCount(); id++) {
            if (!labels[id].empty())
                output += std::format("@{}:\n", labels[id]);
            for (ir::Theorem theorem : theoremsAt[id]) {
                if (!theoremHidden[theorem.id()])
                    emitTheorem(theorem);
            }
            // The last position is the one past the last instruction
            if (id + 1 < positionCount())
                emitInstruction(ir::CodePos(id));
        }
    }

    void emitInstruction(ir::CodePos pos) {
        switch (ir.opcodeAt(pos)) {
        case ir::Opcode::Nop:
            emitLine(INDENT, "nop");
            return;
        case ir::Opcode::Store: {
            auto store = ir.getStore(pos);
            emitStatement(INDENT, std::format("store {} <- ", exprText(store.loc)), store.value, "");
            return;
        }
        case ir::Opcode::Call: {
            auto call = ir.getCall(pos);
            std::string line = std::format("call {}(", exprText(call.target));
            std::span<const ir::Expr> args = ir.view(call.args);
            for (int_t i = 0; i < (int_t)args.size(); i++) {
                if (i > 0)
                    line += ", ";
                appendExpr(line, args[i], Precedence::Or);
            }
            line += ')';
            emitLine(INDENT, line);
            return;
        }
        case ir::Opcode::Phi: {
            std::string line = "phi ";
            ir::ControlFlowEdgeList edges = ir.incomingEdges(pos);
            for (int_t i = 0; i < edges.size(); i++) {
                if (i > 0)
                    line += ", ";
                line += std::format("@{}", label(ir.edgeSource(edges.at(i))));
            }
            emitLine(INDENT, line);
            return;
        }
        case ir::Opcode::Jump:
            emitLine(INDENT, std::format("jump @{}", label(ir.getJump(pos).target)));
            return;
        case ir::Opcode::Branch: {
            auto branch = ir.getBranch(pos);
            emitStatement(INDENT, "branch ", branch.cond,
                std::format(", @{}, @{}", label(branch.ifTrue), label(branch.ifFalse)));
            return;
        }
        }
        VERIFY_NOT_REACHED();
    }

    void emitTheorem(ir::Theorem theorem) {
        ir::Proof proof = ir.proof(theorem);
        bool isPreCondition = proof.tactic() == ir::Tactic::Precondition;

        std::string prefix;
        if (isPreCondition)
            prefix = "pre ";
        else if (postCondition[theorem.id()])
            prefix = "post ";
        else
            prefix = "prove ";
        if (!theoremNames[theorem.id()].empty())
            prefix += std::format("%{}: ", theoremNames[theorem.id()]);

        // A precondition is taken for granted, it is the only theorem without a proof
        if (isPreCondition)
            emitStatement(INDENT, prefix, ir.prop(theorem), "");
        else
            emitProven(INDENT, prefix, ir.prop(theorem), proof);
    }

    //! Writes a proposition together with the proof establishing it
    void emitProven(int_t indent, std::string_view prefix, ir::Bool prop, ir::Proof proof) {
        bool isSat = proof.tactic() == ir::Tactic::Sat;
        std::string suffix = std::format(" by {}{}", tacticName(proof.tactic()), isSat ? ":" : "");
        bool wrapped = emitStatement(indent, prefix, prop, suffix);
        if (!isSat)
            return;

        // The continuation lines of a wrapped proposition have opened a scope of their own
        // already, so the clauses have to be indented past it to open theirs
        int_t clauseIndent = indent + (wrapped ? 2 * INDENT : INDENT);
        for (ir::Theorem clause : ir.getSat(proof).clauses) {
            if (theoremHidden[clause.id()])
                emitProven(clauseIndent, "clause ", ir.prop(clause), ir.proof(clause));
            else
                emitLine(clauseIndent, std::format("clause %{}", theoremNames[clause.id()]));
        }
    }

    void emitLine(int_t indent, std::string_view line) {
        output.append(indent, ' ');
        output += line;
        output += '\n';
    }

    //! Writes one statement, breaking a connective that does not fit onto continuation lines
    /*!
    Returns whether the statement was broken. Only the outermost connective is broken, and only
    where an operand of it may begin a line, so the parser reads the continuations back as part
    of the same expression.
    */
    bool emitStatement(int_t indent, std::string_view prefix, ir::Expr expr, std::string_view suffix) {
        size_t lineBegin = output.size();
        output.append(indent, ' ');
        output += prefix;
        size_t exprBegin = output.size();
        appendExpr(output, expr, Precedence::Or);
        output += suffix;

        bool isConnective = (expr.kind() == ir::ExprKind::And || expr.kind() == ir::ExprKind::Or) && !ir::Bool(expr).negated();
        if ((int_t)(output.size() - lineBegin) <= LINE_LIMIT || !isConnective) {
            output += '\n';
            return false;
        }

        output.resize(exprBegin);
        std::span<const ir::Expr> operands = connectiveOperands(expr);
        Precedence context = operandPrecedence(expr);
        std::string_view connective = expr.kind() == ir::ExprKind::And ? "and " : "or ";
        appendExpr(output, operands[0], context);
        for (int_t i = 1; i < (int_t)operands.size(); i++) {
            output += '\n';
            output.append(indent + INDENT, ' ');
            output += connective;
            appendExpr(output, operands[i], context);
        }
        output += suffix;
        output += '\n';
        return true;
    }

    std::string exprText(ir::Expr expr) const {
        std::string text;
        appendExpr(text, expr, Precedence::Or);
        return text;
    }

    void appendExpr(std::string& text, ir::Expr expr, Precedence context) const {
        bool parenthesize = precedenceOf(expr) < context;
        if (parenthesize)
            text += '(';
        appendExprBody(text, expr);
        if (parenthesize)
            text += ')';
    }

    static Precedence precedenceOf(ir::Expr expr) {
        switch (expr.kind()) {
        case ir::ExprKind::Or:
            return ir::Bool(expr).negated() ? Precedence::Unary : Precedence::Or;
        case ir::ExprKind::And:
            return ir::Bool(expr).negated() ? Precedence::Unary : Precedence::And;
        // A negated equality is spelled '!=' and a negated literal by the value it stands for
        case ir::ExprKind::Equality:
            return Precedence::Equality;
        case ir::ExprKind::BooleanLiteral:
            return Precedence::Postfix;
        default:
            // This is not guaranteed to be a bool, but non-bools are never negated.
            return ir::Bool(expr).negated() ? Precedence::Unary : Precedence::Postfix;
        }
    }

    void appendExprBody(std::string& text, ir::Expr expr) const {
        switch (expr.kind()) {
        case ir::ExprKind::BooleanLiteral:
            text += ir::Bool(expr).negated() ? "true" : "false";
            return;
        case ir::ExprKind::Equality: {
            auto equality = ir.getEquality((ir::Bool)expr);
            appendExpr(text, equality.left, Precedence::Unary);
            text += ir::Bool(expr).negated() ? " != " : " = ";
            appendExpr(text, equality.right, Precedence::Unary);
            return;
        }
        case ir::ExprKind::And:
        case ir::ExprKind::Or: {
            if (ir::Bool(expr).negated()) {
                text += "!(";
                appendConnective(text, expr);
                text += ')';
            } else {
                appendConnective(text, expr);
            }
            return;
        }
        default:
            break;
        }

        // This is not guaranteed to be a bool, but non-bools are never negated.
        if (ir::Bool(expr).negated())
            text += '!';

        if (ir::isLoad(expr.kind())) {
            auto load = ir.getLoad(expr);
            appendExpr(text, load.loc, Precedence::Postfix);
            text += std::format(".load@{}", label(load.pos.simplePos()));
            return;
        }
        switch (expr.kind()) {
        case ir::ExprKind::FunctionParameter:
            text += std::format("${}", parameterNames[expr.id()]);
            return;
        case ir::ExprKind::PositionActive:
            text += std::format("@{}.active", label(ir::CodePos(expr.id())));
            return;
        case ir::ExprKind::ControlFlowEdgeTaken: {
            ir::ControlFlowEdge edge(expr.id());
            text += std::format("@{}.from@{}", label(ir.edgeTarget(edge)), label(ir.edgeSource(edge)));
            return;
        }
        case ir::ExprKind::ScalarType: {
            auto scalar = ir.getScalarType((ir::Bool)expr);
            appendExpr(text, scalar.type, Precedence::Postfix);
            text += std::format(".{}_scalar", sortName(scalar.sort));
            return;
        }
        case ir::ExprKind::MemoryLocType:
            appendExpr(text, ir.getMemoryLocType((ir::Type)expr).loc, Precedence::Postfix);
            text += ".type";
            return;
        default:
            VERIFY_NOT_REACHED(); // The expression has no syntax in the language yet
        }
    }

    void appendConnective(std::string& text, ir::Expr expr) const {
        std::span<const ir::Expr> operands = connectiveOperands(expr);
        Precedence context = operandPrecedence(expr);
        std::string_view connective = expr.kind() == ir::ExprKind::And ? " and " : " or ";
        for (int_t i = 0; i < (int_t)operands.size(); i++) {
            if (i > 0)
                text += connective;
            appendExpr(text, operands[i], context);
        }
    }

    std::span<const ir::Expr> connectiveOperands(ir::Expr expr) const {
        return ir.view(expr.kind() == ir::ExprKind::And
                ? ir.getAnd((ir::Bool)expr).operands
                : ir.getOr((ir::Bool)expr).operands);
    }

    static Precedence operandPrecedence(ir::Expr expr) {
        return expr.kind() == ir::ExprKind::And ? Precedence::Equality : Precedence::And;
    }

    const std::string& label(ir::CodePos pos) const {
        VERIFY(pos.id() < (uint32_t)positionCount());
        const std::string& name = labels[pos.id()];
        VERIFY(!name.empty()); // Every position that is referred to was named
        return name;
    }

    int_t positionCount() const { return ir.here().id() + 1; }

    const ParsedFunction& parsedNames;
    const ir::Function& ir;

    //! How many proofs rest on a theorem, indexed by its id
    std::vector<int_t> theoremMentions;
    std::vector<bool> theoremHidden;
    std::vector<bool> postCondition;
    std::vector<bool> referencedPositions;
    std::vector<bool> visitedCompoundExpressions;

    //! The names to write, the ones of the source with the generated ones filled in
    std::vector<std::string> labels;
    std::vector<std::string> theoremNames;
    std::vector<std::string> parameterNames;

    std::string output;
};

std::string format(const ParsedFunction& parsed) {
    return Formatter(parsed).run();
}

//! Formatting a source has to reproduce it and formatting the result has to change nothing
/*!
The sources are written as raw strings beginning with a newline, so the expected output is the
source without its first character.
*/
static void expectFormatted(const char* source) {
    std::string formatted = format(parse(source));
    EXPECT_EQ(formatted, source + 1);
    EXPECT_EQ(format(parse(formatted.c_str())), source + 1);
}

TEST(VerifyLanguage, FormatInstructions) {
    expectFormatted(R"(
fn #test($f: fn, $a, $b: bool):
@entry:
    nop
    store $a <- $a
    call $f($a, $b)
    call $f()
    jump @loop
@loop:
    phi @entry, @loop_branch
@loop_branch:
    branch $b, @loop, @exit
@exit:
    phi @loop_branch
)");
}

TEST(VerifyLanguage, FormatUnreferencedLabel) {
    // A label nothing refers to is still the name the source gave that position
    expectFormatted(R"(
fn #test($a):
@only_read_by_a_human:
    nop
)");
}

TEST(VerifyLanguage, FormatExpressions) {
    expectFormatted(R"(
fn #test($a, $b, $c, $d):
    store $a <- true
    store $a <- false
    store $a <- $a != $b
    store $a <- ($a = $b) = ($c = $d)
    store $a <- $a = $b and $c = $d or $a = $c
    store $a <- $a = $b and ($c = $d or $a = $c)
    store $a <- !($a = $b and $c = $d)
    store $a <- ($a != $b) = $c
)");

    // A negated equality is spelled with '!=' however the source wrote it
    EXPECT_EQ(format(parse(R"(
fn #test($a, $b, $c):
    store $a <- !($a = $b) = $c
)")),
        "fn #test($a, $b, $c):\n    store $a <- ($a != $b) = $c\n");
}

TEST(VerifyLanguage, FormatPositionExpressions) {
    expectFormatted(R"(
fn #test($x, $y):
@entry:
    pre %x_scalar: $x.type.memory_loc_scalar
    store $y <- $x.load@entry
    jump @next
@next:
    phi @entry
    prove !@next.active or @next.from@entry by phi_enumerate
)");
}

TEST(VerifyLanguage, FormatTheorems) {
    expectFormatted(R"(
fn #test($a, $b):
    pre %a_eq_b: $a = $b
    prove %reflex: $a = $a by eq_reflexive
    prove $b = $a by eq_transitive
    post %both: $a = $a and $b = $b by sat:
        clause %a_eq_b
        clause $b != $a by sorry
        clause %reflex
)");
}

TEST(VerifyLanguage, FormatSatClauses) {
    // A clause the source wrote inside the proof stays there, one it stated on its own does not
    // move in either
    expectFormatted(R"(
fn #test($a, $b):
@entry:
    prove %a_eq_a: $a = $a by eq_reflexive
    jump @exit
@exit:
    prove %b_eq_b: $b = $b by eq_reflexive
    prove true by sat:
        clause %a_eq_a
        clause %b_eq_b
        clause $a = $b by sorry
)");
}

TEST(VerifyLanguage, FormatSharedClause) {
    ParsedFunction parsed = parse(R"(
fn #test($a, $b):
    prove true by sat:
        clause $a = $b by sorry
)");
    // A second proof resting on the same clause: it cannot be inlined into both, and a clause
    // carries no name where it is inlined, so it has to be stated on its own
    ir::Function& fn = parsed.function;
    ir::Expr a(ir::ExprKind::FunctionParameter, 0);
    ir::Expr b(ir::ExprKind::FunctionParameter, 1);
    fn.addTheorem(fn.addEquality({ b, a }), fn.here(), fn.addSat({ { ir::Theorem(0) } }));

    EXPECT_EQ(format(parsed), R"(fn #test($a, $b):
    prove %0: $a = $b by sorry
    prove true by sat:
        clause %0
    prove $b = $a by sat:
        clause %0
)");
}

TEST(VerifyLanguage, FormatGeneratedNames) {
    ParsedFunction parsed = parse(R"(
fn #test($a, $b):
@entry:
    pre %x: $a = $b
    jump @exit
@exit:
    prove %0: !@entry.active by sorry
    prove true by sat:
        clause %x
)");
    // Whatever the source did not name is named after the lowest number that is free. The
    // theorems and the labels are counted apart, '%0' does not stand in the way of '@0'.
    parsed.labels[0].clear();
    parsed.theoremNames[0].clear();

    EXPECT_EQ(format(parsed), R"(fn #test($a, $b):
@0:
    pre %1: $a = $b
    jump @exit
@exit:
    prove %0: !@0.active by sorry
    prove true by sat:
        clause %1
)");
}

TEST(VerifyLanguage, FormatWrapping) {
    // A statement past the line limit is broken before the operands of its outermost connective
    expectFormatted(R"(
fn #test($first_operand, $second_operand, $third_operand):
    prove $first_operand = $second_operand
        or $second_operand = $third_operand
        or $first_operand = $third_operand by eq_transitive
    store $first_operand <- $first_operand = $second_operand
        or $second_operand = $third_operand
        or $first_operand = $third_operand
)");
}

TEST(VerifyLanguage, FormatWrappedSatProposition) {
    // The clauses are indented past the continuation lines, which are a scope of their own
    expectFormatted(R"(
fn #test($first_operand, $second_operand, $third_operand):
    prove %wrapped: $first_operand = $second_operand
        or $second_operand = $third_operand
        or $first_operand = $third_operand by sat:
            clause $first_operand = $second_operand by sorry
            clause $second_operand = $third_operand by sorry
)");
}

}
