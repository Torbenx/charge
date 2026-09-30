#include <verify/language/Lexer.h>
#include <verify/language/ParseContext.h>

#include <format>

namespace verify::language {

struct FunctionParser {
    struct UnresolvedLabel {
        struct Use {
            // The instruction referring to the not-yet-resolved label.
            ir::CodePos instruction;
            // Which slot of the instruction the label fills: always 0 for a
            // jump, 0 (true) or 1 (false) for a branch, the incoming edge index for a phi.
            uint32_t index;
        };

        std::vector<Use> uses;
    };

    // Compactly stores either a resolved CodePos or the index of an UnresolvedLabel
    struct LabelInfo {
        static LabelInfo resolved(ir::CodePos pos) {
            return { .resolvedBit = 1, .valueBits = pos.id() };
        }
        static LabelInfo unresolved(uint32_t index) {
            return { .resolvedBit = 0, .valueBits = index };
        }

        bool isResolved() const { return resolvedBit; }

        ir::CodePos codePos() const {
            VERIFY(resolvedBit);
            return ir::CodePos(valueBits);
        }
        uint32_t unresolvedIndex() const {
            VERIFY(!resolvedBit);
            return valueBits;
        }

        uint32_t resolvedBit : 1;
        uint32_t valueBits : 31;
    };

    FunctionParser(ParsedFunction& out, const IdentifierTable& wordTable)
        : ir(out.function), out(out), wordTable(wordTable) { }

    void parse(TokenStream& s);

    ir::Sort parseSort(TokenStream& s);

    void parseInstructions(TokenStream s);
    void parseStore(TokenStream& s);
    void parseCall(TokenStream& s);
    void parseJump(TokenStream& s);
    void parseBranch(TokenStream& s);
    void parsePhi(TokenStream& s);
    void parseNop(TokenStream& s);

    ir::Theorem parseTheorem(TokenStream& s, bool isPreCondition);

    ir::Proof parseProof(TokenStream& s);

    std::vector<ir::Theorem> parseSatClauses(TokenStream s);

    //! A clause either names a theorem that is already stated or states one of its own
    ir::Theorem parseSatClause(TokenStream& s);

    template<typename T>
    T sortCast(ir::Expr e) {
        return (T)e;
    }

    ir::Expr parseExpression(TokenStream& s) {
        return parseOrExpr(s);
    }

    template<typename T>
    T parseExpression(TokenStream& s) {
        return sortCast<T>(parseExpression(s));
    }

    template<typename Parse>
    std::vector<ir::Expr> parseInfixOperands(TokenStream& s, Word connective, Parse parseOperand) {
        std::vector<ir::Expr> operands { parseOperand(s) };
        while (s.tokKind() == TokenKind::Identifier && s.tok().word() == connective) {
            s.advance();
            operands.push_back(parseOperand(s));
        }
        return operands;
    }

    template<typename Parse>
    std::vector<ir::Bool> parseConnectiveOperands(TokenStream& s, Word connective, Parse parseOperand) {
        std::vector<ir::Bool> operands;
        for (ir::Expr operand : parseInfixOperands(s, connective, parseOperand))
            operands.push_back(sortCast<ir::Bool>(operand));
        return operands;
    }

    ir::Expr parseOrExpr(TokenStream& s);
    ir::Expr parseAndExpr(TokenStream& s);
    ir::Expr parseEqualityExpr(TokenStream& s);
    ir::Sort setSortOf(TokenStream& s, ir::Expr operand);
    ir::Expr parseUnionExpr(TokenStream& s);
    //! A chain of 'setminus' groups to the left, the operator is not associative
    ir::Expr parseSetMinusExpr(TokenStream& s);
    ir::Expr parseIntersectionExpr(TokenStream& s);

    ir::Expr parseUnaryExpr(TokenStream& s);

    ir::Expr parsePostfixExpr(TokenStream& s);
    //! The sort of a load is inferred from a 'scalarType' theorem of the loaded location
    ir::Expr parseLoad(TokenStream& s, ir::MemoryLoc loc);

    //! The sets a memory location describes, together with the empty set of either set sort
    /*!
    'mset()' and 'iset()' are the empty sets. A set has a sort, so there is one of them per set
    sort and neither is written without saying which one it is.
    */
    ir::Expr parseSetConstructor(TokenStream& s, Word id);

    ir::Expr parsePrimaryExpression(TokenStream& s);

    // Resolves the label token under the cursor, requiring it to already be
    // defined. Unlike getLabelForInstruction, forward references are not allowed here.
    ir::CodePos getLabel(TokenStream& s);

    // Resolves the label token under the cursor. If the label is already known
    // its position is returned. Otherwise INVALID_CODE_POS is returned as a
    // placeholder and the use is recorded on the unresolved label so it can be
    // patched into 'instruction' at slot 'index' once the label is defined.
    ir::CodePos getLabelForInstruction(TokenStream& s, ir::CodePos instruction, uint32_t index);

    void recordLabelName(ir::CodePos pos, Word name);
    void recordTheoremName(ir::Theorem theorem, Word name);
    void defineLabel(Word name, ir::CodePos pos);

    //! Reports the labels that were referenced by an instruction but never defined
    void checkLabelsResolved();

    ir::Function& ir;
    ParsedFunction& out;
    const IdentifierTable& wordTable;
    LookupTable<ir::Theorem> theorems;
    LookupTable<LabelInfo> labels;
    LookupTable<ir::Expr> locals;
    std::vector<UnresolvedLabel> unresolvedLabels;
};

void FunctionParser::parse(TokenStream& s) {
    VERIFY(s.tokKind() == TokenKind::Identifier);
    VERIFY(s.tok().word() == words["fn"]);
    s.advance();

    if (s.tokKind() != TokenKind::GlobalName)
        s.error("Expected global name after 'fn'");
    out.name = wordTable.view(s.tok().word());
    s.advance();
    if (s.tokKind() != TokenKind::LeftParen)
        s.error("Expected '(' after function name");
    s.advance();
    if (s.tokKind() != TokenKind::RightParen) {
        for (;;) {
            if (s.tokKind() != TokenKind::LocalName)
                s.error("Expected parameter name");
            Word name = s.tok().word();
            s.advance();
            // A parameter without a sort is a memory location
            ir::Sort sort = ir::Sort::MemoryLoc;
            if (s.tokKind() == TokenKind::Colon) {
                s.advance();
                sort = parseSort(s);
            }
            locals.insert(name, ir.addParameter(sort));
            out.parameterNames.emplace_back(wordTable.view(name));
            if (s.tokKind() == TokenKind::Comma) {
                s.advance();
                continue;
            } else if (s.tokKind() == TokenKind::RightParen) {
                break;
            } else {
                s.error("Unexpected token after parameter");
            }
        }
    }
    VERIFY(s.tokKind() == TokenKind::RightParen);
    s.advance();

    VERIFY(s.tokKind() == TokenKind::Colon);
    s.advanceWithNoScopeChanges();

    if (s.tokKind() != TokenKind::BeginScope)
        s.error("Expected function body");
    parseInstructions(s);
}

ir::Sort FunctionParser::parseSort(TokenStream& s) {
    if (s.tokKind() != TokenKind::Identifier)
        s.error("Expected sort name");
    Word id = s.tok().word();
    s.advance();
#define SORT(name, snake_case)    \
    if (id == words[#snake_case]) \
        return ir::Sort::name;
#include <verify/ir/sorts.inc>
    s.error("Unknown sort");
}

void FunctionParser::parseInstructions(TokenStream s) {
    for (;;) {
        switch (s.tokKind()) {
        case TokenKind::EndScope:
            return;
        case TokenKind::ContinueScope:
            if (!s.tok().word().empty())
                defineLabel(s.tok().word(), ir.here());
            s.advance();
            continue;
        case TokenKind::Identifier: {
            Word id = s.tok().word();
            if (id == words["store"]) {
                parseStore(s);
            } else if (id == words["call"]) {
                parseCall(s);
            } else if (id == words["jump"]) {
                parseJump(s);
            } else if (id == words["branch"]) {
                parseBranch(s);
            } else if (id == words["phi"]) {
                parsePhi(s);
            } else if (id == words["nop"]) {
                parseNop(s);
            } else if (id == words["pre"]) {
                parseTheorem(s, true);
            } else if (id == words["post"]) {
                ir.addPostCondition(parseTheorem(s, false));
            } else if (id == words["prove"]) {
                parseTheorem(s, false);
            } else {
                s.error("Unknown instruction");
            }
            continue;
        }
        default:
            s.error("Expected instruction");
        }
    }
}

void FunctionParser::parseStore(TokenStream& s) {
    VERIFY(s.tokKind() == TokenKind::Identifier);
    VERIFY(s.tok().word() == words["store"]);
    s.advance();
    ir::MemoryLoc loc = parseExpression<ir::MemoryLoc>(s);
    if (s.tokKind() != TokenKind::LeftArrow)
        s.error("Expected '<-' after store location");
    s.advance();
    ir::Expr value = parseExpression(s);
    ir.addStore({ .loc = loc, .value = value });
}

void FunctionParser::parseCall(TokenStream& s) {
    VERIFY(s.tokKind() == TokenKind::Identifier);
    VERIFY(s.tok().word() == words["call"]);
    s.advance();
    ir::Fn target = parseExpression<ir::Fn>(s);
    if (s.tokKind() != TokenKind::LeftParen)
        s.error("Expected '(' after call target");
    s.advance();
    std::vector<ir::Expr> args;
    if (s.tokKind() != TokenKind::RightParen) {
        for (;;) {
            args.push_back(parseExpression(s));
            if (s.tokKind() == TokenKind::Comma) {
                s.advance();
                continue;
            } else if (s.tokKind() == TokenKind::RightParen) {
                break;
            } else {
                s.error("Expected ',' or ')' in call arguments");
            }
        }
    }
    VERIFY(s.tokKind() == TokenKind::RightParen);
    s.advance();
    ir.addCall({ .target = target, .args = ir.makeExprList(args) });
}

void FunctionParser::parseJump(TokenStream& s) {
    VERIFY(s.tokKind() == TokenKind::Identifier);
    VERIFY(s.tok().word() == words["jump"]);
    s.advance();
    if (s.tokKind() != TokenKind::LabelName)
        s.error("Expected label after 'jump'");
    ir::CodePos target = getLabelForInstruction(s, ir.here(), 0);
    s.advance();
    ir.addJump({ .target = target });
}

void FunctionParser::parseBranch(TokenStream& s) {
    VERIFY(s.tokKind() == TokenKind::Identifier);
    VERIFY(s.tok().word() == words["branch"]);
    s.advance();
    ir::Bool cond = parseExpression<ir::Bool>(s);
    if (s.tokKind() != TokenKind::Comma)
        s.error("Expected ',' after branch condition");
    s.advance();
    if (s.tokKind() != TokenKind::LabelName)
        s.error("Expected true target label after branch condition");
    ir::CodePos ifTrue = getLabelForInstruction(s, ir.here(), 0);
    s.advance();
    if (s.tokKind() != TokenKind::Comma)
        s.error("Expected ',' between branch targets");
    s.advance();
    if (s.tokKind() != TokenKind::LabelName)
        s.error("Expected false target label");
    ir::CodePos ifFalse = getLabelForInstruction(s, ir.here(), 1);
    s.advance();
    ir.addBranch({ .cond = cond, .ifTrue = ifTrue, .ifFalse = ifFalse });
}

void FunctionParser::parsePhi(TokenStream& s) {
    VERIFY(s.tokKind() == TokenKind::Identifier);
    VERIFY(s.tok().word() == words["phi"]);
    s.advance();
    std::vector<ir::CodePos> sources;
    for (uint32_t edgeIndex = 0;; edgeIndex++) {
        if (s.tokKind() != TokenKind::LabelName)
            s.error("Expected source label in phi");
        sources.push_back(getLabelForInstruction(s, ir.here(), edgeIndex));
        s.advance();
        if (s.tokKind() == TokenKind::Comma) {
            s.advance();
            continue;
        }
        break;
    }
    ir.addPhi(sources);
}

void FunctionParser::parseNop(TokenStream& s) {
    VERIFY(s.tokKind() == TokenKind::Identifier);
    VERIFY(s.tok().word() == words["nop"]);
    s.advance();
    ir.addNop({});
}

ir::Theorem FunctionParser::parseTheorem(TokenStream& s, bool isPreCondition) {
    VERIFY(s.tokKind() == TokenKind::Identifier);
    s.advance();
    Word name;
    if (s.tokKind() == TokenKind::TheoremName) {
        name = s.tok().word();
        s.advance();
        if (s.tokKind() != TokenKind::Colon)
            s.error("Expected ':' after theorem name");
        s.advance();
    }
    ir::Bool prop = parseExpression<ir::Bool>(s);

    // The proof is read before the theorem is added, so that the theorems its clauses
    // state come before it and the order of the list is an order to check them in.
    // The theorem does not exist yet either, so a proof cannot rest on itself.
    std::optional<ir::Proof> proof;
    if (!isPreCondition)
        proof = parseProof(s);

    // This also catches a clause of the proof restating the proposition
    if (ir.findTheorem(prop).has_value())
        s.error("Proposition was already stated by another theorem");

    ir::Theorem theorem = isPreCondition
        ? ir.addPreCondition(prop, ir.here())
        : ir.addTheorem(prop, ir.here(), proof.value());
    if (!name.empty()) {
        theorems.insert(name, theorem);
        recordTheoremName(theorem, name);
    }
    return theorem;
}

ir::Proof FunctionParser::parseProof(TokenStream& s) {
    if (s.tokKind() != TokenKind::Identifier && s.tok().word() != words["by"])
        s.error("Expect 'by' after theorem");
    s.advance();
    if (s.tokKind() != TokenKind::Identifier)
        s.error("Expected tactic name after 'by'");
    Word id = s.tok().word();
    s.advance();

    if (id == words["sat"]) {
        if (s.tokKind() != TokenKind::Colon)
            s.error("Expected ':' after 'sat'");
        s.advanceWithNoScopeChanges();
        return ir.addSat({ parseSatClauses(s) });
    }
#define SIMPLE_TACTIC(name, snake_case) \
    if (id == words[#snake_case])       \
        return ir::Proof::make##name();
#include <verify/ir/tactics.inc>
    s.error("Unknown tactic");
}

std::vector<ir::Theorem> FunctionParser::parseSatClauses(TokenStream s) {
    std::vector<ir::Theorem> clauses;
    for (;;) {
        switch (s.tokKind()) {
        case TokenKind::EndScope:
            return clauses;
        case TokenKind::ContinueScope:
            if (!s.tok().word().empty())
                s.error("Invalid label location");
            s.advance();
            continue;
        case TokenKind::Identifier: {
            if (s.tok().word() != words["clause"])
                s.error("Expected clause");
            s.advance();
            clauses.push_back(parseSatClause(s));
            continue;
        }
        default:
            s.error("Expected clause");
        }
    }
}

//! A clause either names a theorem that is already stated or states one of its own
ir::Theorem FunctionParser::parseSatClause(TokenStream& s) {
    if (s.tokKind() == TokenKind::TheoremName) {
        auto theorem = theorems.get(s.tok().word());
        if (!theorem.has_value())
            s.error("Theorem must be stated before use");
        s.advance();
        return theorem.value();
    }

    ir::Bool prop = parseExpression<ir::Bool>(s);
    ir::Proof proof = parseProof(s);
    if (ir.findTheorem(prop).has_value())
        s.error("Proposition was already stated by another theorem");
    return ir.addTheorem(prop, ir.here(), proof);
}

ir::Expr FunctionParser::parseOrExpr(TokenStream& s) {
    auto operands = parseConnectiveOperands(s, words["or"], [this](TokenStream& s) { return parseAndExpr(s); });
    if (operands.size() == 1)
        return operands.front();
    return ir.addOr(operands);
}

ir::Expr FunctionParser::parseAndExpr(TokenStream& s) {
    auto operands = parseConnectiveOperands(s, words["and"], [this](TokenStream& s) { return parseEqualityExpr(s); });
    if (operands.size() == 1)
        return operands.front();
    return ir.addAnd(operands);
}

ir::Expr FunctionParser::parseEqualityExpr(TokenStream& s) {
    ir::Expr left = parseUnionExpr(s);
    if (s.tokKind() == TokenKind::Equal) {
        s.advance();
        ir::Expr right = parseUnionExpr(s);
        return ir.addEquality({ left, right });
    } else if (s.tokKind() == TokenKind::ExclaimEqual) {
        s.advance();
        ir::Expr right = parseUnionExpr(s);
        return !ir.addEquality({ left, right });
    } else {
        return left;
    }
}

ir::Sort FunctionParser::setSortOf(TokenStream& s, ir::Expr operand) {
    ir::Sort sort = ir.sortOf(operand);
    if (!ir::isSetSort(sort))
        s.error("Operand of a set operator is not a set");
    return sort;
}

ir::Expr FunctionParser::parseUnionExpr(TokenStream& s) {
    auto operands = parseInfixOperands(s, words["union"], [this](TokenStream& s) { return parseSetMinusExpr(s); });
    if (operands.size() == 1)
        return operands.front();
    return ir.addUnion(setSortOf(s, operands.front()), operands);
}

//! A chain of 'setminus' groups to the left, the operator is not associative
ir::Expr FunctionParser::parseSetMinusExpr(TokenStream& s) {
    ir::Expr base = parseIntersectionExpr(s);
    while (s.tokKind() == TokenKind::Identifier && s.tok().word() == words["setminus"]) {
        s.advance();
        ir::Expr subtrahend = parseIntersectionExpr(s);
        base = ir.addSetMinus(setSortOf(s, base), { base, subtrahend });
    }
    return base;
}

ir::Expr FunctionParser::parseIntersectionExpr(TokenStream& s) {
    auto operands = parseInfixOperands(s, words["intersection"], [this](TokenStream& s) { return parseUnaryExpr(s); });
    if (operands.size() == 1)
        return operands.front();
    return ir.addIntersection(setSortOf(s, operands.front()), operands);
}

ir::Expr FunctionParser::parseUnaryExpr(TokenStream& s) {
    if (s.tokKind() == TokenKind::Exclaim) {
        s.advance();
        return !sortCast<ir::Bool>(parseUnaryExpr(s));
    } else {
        return parsePostfixExpr(s);
    }
}

ir::Expr FunctionParser::parsePostfixExpr(TokenStream& s) {
    ir::Expr base = parsePrimaryExpression(s);
    while (s.tokKind() == TokenKind::Point) {
        s.advance();
        if (s.tokKind() != TokenKind::Identifier)
            s.error("Unexpected token after '.'");
        Word id = s.tok().word();
        s.advance();
        if (id == words["load"]) {
            base = parseLoad(s, sortCast<ir::MemoryLoc>(base));
            continue;
        }
        if (id == words["type"]) {
            base = ir.addMemoryLocType({ sortCast<ir::MemoryLoc>(base) });
            continue;
        }
#define SORT(name, snake_case)                                                 \
    if (id == words[#snake_case "_scalar"]) {                                  \
        base = ir.addScalarType({ sortCast<ir::Type>(base), ir::Sort::name }); \
        continue;                                                              \
    }
#include <verify/ir/sorts.inc>
        s.error("Unexpected identifier in postfix expression");
    }
    return base;
}

//! The sort of a load is inferred from a 'scalarType' theorem of the loaded location
ir::Expr FunctionParser::parseLoad(TokenStream& s, ir::MemoryLoc loc) {
    if (s.tokKind() != TokenKind::LabelName)
        s.error("Expected label name after load");
    std::optional<ir::Sort> sort = ir.scalarSort(loc);
    if (!sort.has_value())
        s.error("The loaded location must be proven scalar before it is loaded");
    ir::CodePos pos = getLabel(s);
    s.advance();
    return ir.addLoad(*sort, { loc, pos });
}

//! The sets a memory location describes, together with the empty set of either set sort
/*!
'mset()' and 'iset()' are the empty sets. A set has a sort, so there is one of them per set
sort and neither is written without saying which one it is.
*/
ir::Expr FunctionParser::parseSetConstructor(TokenStream& s, Word id) {
    if (s.tokKind() != TokenKind::LeftParen)
        s.error("Expected '(' after a set constructor");
    s.advance();

    if (s.tokKind() == TokenKind::RightParen) {
        s.advance();
        if (id == words["mset"])
            return ir.emptySet(ir::Sort::MemorySet);
        if (id == words["iset"])
            return ir.emptySet(ir::Sort::InvariantSet);
        s.error("Expected a memory location in a set constructor");
    }
    // The invariant sets of a location are the inclusive, the exclusive, the path and the exact one,
    // so 'iset' is only ever written as the empty set
    if (id == words["iset"])
        s.error("Expected ')' after 'iset(', an invariant set of a location is written with 'incl_iset', 'excl_iset', 'path_iset' or 'exact_iset'");

    ir::MemoryLoc loc = parseExpression<ir::MemoryLoc>(s);

    if (id == words["exact_iset"]) {
        if (s.tokKind() != TokenKind::Comma)
            s.error("Expected ',' after the location of an exact invariant set");
        s.advance();
        if (s.tokKind() != TokenKind::GlobalName)
            s.error("Expected the global name of an invariant");
        s.advance();
        // TODO: Resolve the name to the invariant it stands for and build the expression
        // 'ir.addExactInvariantSet({ loc, invariant })' from it
        VERIFY_NOT_REACHED();
    }

    if (s.tokKind() != TokenKind::RightParen)
        s.error("Expected ')' after the location of a set constructor");
    s.advance();

    if (id == words["mset"])
        return ir.addLocationMemorySet({ loc });
    if (id == words["incl_iset"])
        return ir.addInclusiveInvariantSet({ loc });
    if (id == words["excl_iset"])
        return ir.addExclusiveInvariantSet({ loc });
    VERIFY(id == words["path_iset"]);
    return ir.addPathInvariantSet({ loc });
}

static bool isSetConstructor(Word id) {
    return id == words["mset"] || id == words["iset"]
        || id == words["incl_iset"] || id == words["excl_iset"]
        || id == words["path_iset"] || id == words["exact_iset"];
}

ir::Expr FunctionParser::parsePrimaryExpression(TokenStream& s) {
    switch (s.tokKind()) {
    case TokenKind::LeftParen: {
        s.advance();
        ir::Expr e = parseExpression(s);
        if (s.tokKind() != TokenKind::RightParen)
            s.error("Expected ')' after expression");
        s.advance();
        return e;
    }
    case TokenKind::LabelName: {
        ir::CodePos labelPos = getLabel(s);
        s.advance();
        if (s.tokKind() != TokenKind::Point)
            s.error("Expected '.' after label");
        s.advance();
        if (s.tokKind() != TokenKind::Identifier)
            s.error("Expected identifier after label");
        if (s.tok().word() == words["active"]) {
            s.advance();
            return ir::Expr::makePositionActive(labelPos);
        } else if (s.tok().word() == words["from"]) {
            s.advance();
            if (s.tokKind() != TokenKind::LabelName)
                s.error("Expected label after '.from'");
            ir::CodePos sourcePos = getLabel(s);
            s.advance();
            if (ir.opcodeAt(labelPos) != ir::Opcode::Phi)
                s.error("'.from' requires a phi instruction");
            ir::ControlFlowEdgeList incomingEdges = ir.incomingEdges(labelPos);
            for (int_t i = 0; i < incomingEdges.size(); i++) {
                ir::ControlFlowEdge edge = incomingEdges.at(i);
                if (ir.edgeSource(edge) == sourcePos)
                    return ir::Expr::makeControlFlowEdgeTaken(edge);
            }
            s.error("Label is not a source of the referenced phi");
        } else {
            s.error("Invalid identifier after label");
        }
    }
    case TokenKind::LocalName: {
        auto local = locals.get(s.tok().word());
        if (!local.has_value())
            s.error("Local must be defined before use");
        s.advance();
        return local.value();
    }
    case TokenKind::GlobalName:
        VERIFY_NOT_REACHED(); // TODO
    case TokenKind::Identifier: {
        Word id = s.tok().word();
        s.advance();
        if (id == words["false"]) {
            return ir::Bool(false);
        } else if (id == words["true"]) {
            return ir::Bool(true);
        } else if (isSetConstructor(id)) {
            return parseSetConstructor(s, id);
        } else {
            s.error("Invalid identifier for expression");
        }
    }
    default:
        s.error("Expected expression");
    }
}

// Resolves the label token under the cursor, requiring it to already be
// defined. Unlike getLabelForInstruction, forward references are not allowed here.
ir::CodePos FunctionParser::getLabel(TokenStream& s) {
    VERIFY(s.tokKind() == TokenKind::LabelName);
    auto info = labels.get(s.tok().word());
    if (!info.has_value() || !info->isResolved())
        s.error("Label must be defined before use");
    return info->codePos();
}

// Resolves the label token under the cursor. If the label is already known
// its position is returned. Otherwise INVALID_CODE_POS is returned as a
// placeholder and the use is recorded on the unresolved label so it can be
// patched into 'instruction' at slot 'index' once the label is defined.
ir::CodePos FunctionParser::getLabelForInstruction(TokenStream& s, ir::CodePos instruction, uint32_t index) {
    VERIFY(s.tokKind() == TokenKind::LabelName);
    Word name = s.tok().word();
    auto info = labels.get(name);
    if (info.has_value() && info->isResolved())
        return info->codePos();

    uint32_t unresolvedIndex;
    if (info.has_value()) {
        unresolvedIndex = info->unresolvedIndex();
    } else {
        unresolvedIndex = (uint32_t)unresolvedLabels.size();
        unresolvedLabels.emplace_back();
        labels.insert(name, LabelInfo::unresolved(unresolvedIndex));
    }
    unresolvedLabels[unresolvedIndex].uses.push_back({ .instruction = instruction, .index = index });
    return ir::INVALID_CODE_POS;
}

void FunctionParser::recordLabelName(ir::CodePos pos, Word name) {
    if (out.labels.size() <= pos.id())
        out.labels.resize(pos.id() + 1);
    if (out.labels[pos.id()].empty())
        out.labels[pos.id()] = wordTable.view(name);
}

void FunctionParser::recordTheoremName(ir::Theorem theorem, Word name) {
    if (out.theoremNames.size() <= theorem.id())
        out.theoremNames.resize(theorem.id() + 1);
    out.theoremNames[theorem.id()] = wordTable.view(name);
}

void FunctionParser::defineLabel(Word name, ir::CodePos pos) {
    recordLabelName(pos, name);
    auto info = labels.insertOrUpdate(name, LabelInfo::resolved(pos));
    if (!info.has_value())
        return;

    VERIFY(!info->isResolved());
    for (const auto& use : unresolvedLabels[info->unresolvedIndex()].uses) {
        switch (ir.opcodeAt(use.instruction)) {
        case ir::Opcode::Jump:
            VERIFY(use.index == 0);
            ir.setJumpTarget(use.instruction, pos);
            break;
        case ir::Opcode::Branch:
            VERIFY(use.index <= 1);
            if (use.index == 0)
                ir.setBranchTrueTarget(use.instruction, pos);
            else
                ir.setBranchFalseTarget(use.instruction, pos);
            break;
        case ir::Opcode::Phi:
            ir.setEdgeSource(use.instruction, use.index, pos);
            break;
        default:
            VERIFY_NOT_REACHED();
        }
    }
}

//! Reports the labels that were referenced by an instruction but never defined
void FunctionParser::checkLabelsResolved() {
    std::string undefined;
    labels.forEachEntry([this, &undefined](Word name, LabelInfo info) {
        if (info.isResolved())
            return;
        if (!undefined.empty())
            undefined += ", ";
        undefined += std::format("@{}", wordTable.view(name));
    });
    if (!undefined.empty())
        throw ParserException(std::format("Label was never defined: {}", undefined));
}

ParsedFunction ParseContext::parseFunction(TokenStream& s) {
    ParsedFunction result;

    FunctionParser parser { result, wordTable };
    parser.parse(s);
    parser.checkLabelsResolved();

    // The name tables are filled as the names are read, so they end where the last name was
    result.labels.resize(result.function.here().id() + 1);
    result.theoremNames.resize(result.function.theoremCount());

    return result;
}

ParsedFunction parseFunction(const char* source) {
    ParseContext context;
    std::vector<Token> tokens = lexFile(source, context.wordTable);
    auto s = TokenStream::makeRoot(tokens.data());
    VERIFY(s.tokKind() == TokenKind::BeginScope);
    s.advance();
    while (s.tokKind() == TokenKind::ContinueScope)
        s.advance();
    return context.parseFunction(s);
}

}
