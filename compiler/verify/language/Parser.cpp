#include <verify/language/Parser.h>

#include <gtest/gtest.h>

namespace verify::language {

static const char* skipSpaces(const char* position) {
    while (position[0] == ' ')
        position += 1;
    return position;
}

static bool isBulkNameCharacter(char c) {
    return ('A' <= c && c <= 'Z')
        || ('a' <= c && c <= 'z')
        || ('0' <= c && c <= '9')
        || c == '_';
}

enum class TokenKind : uint8_t {
    BeginScope,
    ContinueScope,
    EndScope,

    Identifier,
    LocalName,
    TheoremName,
    LabelName,
    GlobalName,

    LeftParen,
    RightParen,
    Colon,
    Comma,
    Point,
    LeftArrow,
    Exclaim,
    ExclaimEqual,
    Equal,
};

struct Token {
    TokenKind m_kind;
    uint32_t m_data = 0;

    TokenKind kind() const { return m_kind; }

    Word word() const {
        VERIFY(kind() == TokenKind::Identifier
            || kind() == TokenKind::LocalName
            || kind() == TokenKind::TheoremName
            || kind() == TokenKind::LabelName
            || kind() == TokenKind::GlobalName
            || kind() == TokenKind::ContinueScope);
        return Word::fromUint(m_data);
    }
};

struct Lexer {
    void lex(const char* position);
    Word readWord(const char*& position);

    struct ScopeStackEntry {
        uint32_t indent = 0;
    };

    WordStringTable wordTable;
    std::vector<Token> tokens;
    std::vector<ScopeStackEntry> scopeStack;
};

void Lexer::lex(const char* position) {
    scopeStack.push_back({ .indent = 0 });
    tokens.push_back({ TokenKind::BeginScope });

    for (;;) {
        position = skipSpaces(position);
        switch (position[0]) {
        case '\n': {
            position += 1;
            break;
        }
        case '\r': {
            if (position[1] == '\n')
                position += 2;
            else
                position += 1;
            break;
        }
        case '\0':
            break;

        case '(':
            position += 1;
            tokens.push_back({ TokenKind::LeftParen });
            continue;
        case ')':
            position += 1;
            tokens.push_back({ TokenKind::RightParen });
            continue;
        case ':':
            position += 1;
            tokens.push_back({ TokenKind::Colon });
            continue;
        case ',':
            position += 1;
            tokens.push_back({ TokenKind::Comma });
            continue;
        case '.':
            position += 1;
            tokens.push_back({ TokenKind::Point });
            continue;
        case '!':
            if (position[1] == '=') {
                position += 2;
                tokens.push_back({ TokenKind::ExclaimEqual });
                continue;
            } else {
                position += 1;
                tokens.push_back({ TokenKind::Exclaim });
                continue;
            }
        case '<':
            if (position[1] == '-') {
                position += 2;
                tokens.push_back({ TokenKind::LeftArrow });
                continue;
            } else {
                VERIFY_NOT_REACHED();
            }
        case '=':
            position += 1;
            tokens.push_back({ TokenKind::Equal });
            continue;

        case '$':
            position += 1;
            tokens.push_back({ TokenKind::LocalName, readWord(position).toUint() });
            continue;
        case '%':
            position += 1;
            tokens.push_back({ TokenKind::TheoremName, readWord(position).toUint() });
            continue;
        case '@':
            position += 1;
            tokens.push_back({ TokenKind::LabelName, readWord(position).toUint() });
            continue;
        case '#':
            position += 1;
            tokens.push_back({ TokenKind::GlobalName, readWord(position).toUint() });
            continue;
            // clang-format off
            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': case 'J': case 'K': case 'L': case 'M':
            case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm':
            case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
            case '_':
            // clang-format on
            tokens.push_back({ TokenKind::Identifier, readWord(position).toUint() });
            continue;
            // clang-format off
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            // clang-format on
            VERIFY_NOT_REACHED();

        default:
            VERIFY_NOT_REACHED();
        }

        // Handle scope changes
        const char* lineBegin;
        std::vector<Word> labels;
        for (;;) {
            lineBegin = position;
            if (position[0] == '@') {
                position += 1;
                labels.push_back(readWord(position));
                VERIFY(position[0] == ':');
                position += 1;
            }
            position = skipSpaces(position);
            if (position[0] == '\r') {
                if (position[1] == '\n')
                    position += 2;
                else
                    position += 1;
                continue;
            } else if (position[0] == '\n') {
                position += 1;
                continue;
            }
            break;
        }
        uint32_t indent = position - lineBegin;
        if (position[0] == '\0') {
            for ([[maybe_unused]] auto& entry : scopeStack)
                tokens.push_back({ TokenKind::EndScope });
            scopeStack.clear();
            return;
        }
        if (indent > scopeStack.back().indent) {
            tokens.push_back({ TokenKind::BeginScope });
            scopeStack.push_back({ .indent = indent });
            for (Word label : labels)
                tokens.push_back({ TokenKind::ContinueScope, label.toUint() });
        } else {
            while (indent < scopeStack.back().indent) {
                tokens.push_back({ TokenKind::EndScope });
                scopeStack.pop_back();
            }
            VERIFY(indent == scopeStack.back().indent);
            if (labels.empty()) {
                tokens.push_back({ TokenKind::ContinueScope });
            } else {
                for (Word label : labels)
                    tokens.push_back({ TokenKind::ContinueScope, label.toUint() });
            }
        }
    }
}

[[gnu::always_inline]] Word Lexer::readWord(const char*& position) {
    const char* begin = position;
    Word::HashState state;
    while (isBulkNameCharacter(*position)) {
        Word::iterateHash(state, *position);
        position += 1;
    }
    auto hash = Word::finalizeHash(state, position - begin);
    return wordTable.getWithHash({ begin, position }, hash);
}

struct TokenStream {
    TokenStream* parent = nullptr;
    TokenStream* child = nullptr;
    Token* token = nullptr;
    uint32_t inlineDepth = 0;
    bool invalid = false;

    const Token& tok() const { return *token; }
    TokenKind tokKind() const { return token->kind(); }

    static TokenStream makeRoot(Token* stream) {
        return TokenStream(stream);
    }

    TokenStream(TokenStream& parent)
        : parent(&parent) {
        VERIFY(!parent.invalid);
        VERIFY(parent.tokKind() == TokenKind::BeginScope);
        token = parent.token + 1;
        parent.child = this;
    }

    ~TokenStream() {
        VERIFY(child == nullptr);
        if (tokKind() != TokenKind::EndScope)
            invalid = true;
        if (parent != nullptr) {
            VERIFY(parent->child == this);
            parent->child = nullptr;
            if (invalid) {
                parent->invalid = true;
            } else {
                parent->token = token;
                parent->advance();
            }
        }
    }

    void advanceWithNoScopeChanges() {
        VERIFY(child == nullptr);
        VERIFY(!invalid);
        token += 1;
    }

    void advance() {
        VERIFY(child == nullptr);
        VERIFY(!invalid);
        token += 1;
        for (;;) {
            if (tokKind() == TokenKind::BeginScope) {
                inlineDepth += 1;
                token += 1;
                continue;
            } else if (tokKind() == TokenKind::ContinueScope) {
                if (inlineDepth > 0) {
                    if (!tok().word().empty())
                        error("Invalid label location");
                    token += 1;
                    continue;
                }
            } else if (tokKind() == TokenKind::EndScope) {
                if (inlineDepth > 0) {
                    token += 1;
                    inlineDepth -= 1;
                    continue;
                }
            }
            break;
        }
    }

    [[noreturn]] void error(std::string message) {
        throw ParserException(std::move(message));
    }

private:
    explicit TokenStream(Token* stream) // root constructor
        : parent(nullptr), child(nullptr), token(stream) { }
};

struct FunctionParser {
    struct UnresolvedLabel {
        struct Use {
            // The instruction referring to the not-yet-resolved label.
            ir::CodePos instruction;
            // Which slot of the instruction the label fills: always 0 for a
            // jump, 0 (true) or 1 (false) for a branch, the parent index for a phi.
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

    explicit FunctionParser(ir::Function& f)
        : ir(f) { }

    void parse(TokenStream& s) {
        VERIFY(s.tokKind() == TokenKind::Identifier);
        VERIFY(s.tok().word() == words["fn"]);
        s.advance();

        if (s.tokKind() != TokenKind::GlobalName)
            s.error("Expected global name after 'fn'");
        Word fnName = s.tok().word();
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

    ir::Sort parseSort(TokenStream& s) {
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

    void parseInstructions(TokenStream s) {
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

    void parseStore(TokenStream& s) {
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

    void parseCall(TokenStream& s) {
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

    void parseJump(TokenStream& s) {
        VERIFY(s.tokKind() == TokenKind::Identifier);
        VERIFY(s.tok().word() == words["jump"]);
        s.advance();
        if (s.tokKind() != TokenKind::LabelName)
            s.error("Expected label after 'jump'");
        ir::CodePos target = getLabelForInstruction(s, ir.here(), 0);
        s.advance();
        ir.addJump({ .target = target });
    }

    void parseBranch(TokenStream& s) {
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

    void parsePhi(TokenStream& s) {
        VERIFY(s.tokKind() == TokenKind::Identifier);
        VERIFY(s.tok().word() == words["phi"]);
        s.advance();
        std::vector<ir::CodePos> parents;
        for (uint32_t parentIndex = 0;; parentIndex++) {
            if (s.tokKind() != TokenKind::LabelName)
                s.error("Expected parent label in phi");
            parents.push_back(getLabelForInstruction(s, ir.here(), parentIndex));
            s.advance();
            if (s.tokKind() == TokenKind::Comma) {
                s.advance();
                continue;
            }
            break;
        }
        ir.addPhi(parents);
    }

    void parseNop(TokenStream& s) {
        VERIFY(s.tokKind() == TokenKind::Identifier);
        VERIFY(s.tok().word() == words["nop"]);
        s.advance();
        ir.addNop({});
    }

    ir::Theorem parseTheorem(TokenStream& s, bool isPreCondition) {
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
        if (!name.empty())
            theorems.insert(name, theorem);
        return theorem;
    }

    ir::Proof parseProof(TokenStream& s) {
        if (s.tokKind() != TokenKind::Identifier && s.tok().word() != words["by"])
            s.error("Expect 'by' after theorem");
        s.advance();
        if (s.tokKind() != TokenKind::Identifier)
            s.error("Expected tactic name after 'by'");
        Word id = s.tok().word();
        s.advance();

        if (id == words["sorry"]) {
            return ir::Proof::makeSorry();
        } else if (id == words["sat"]) {
            if (s.tokKind() != TokenKind::Colon)
                s.error("Expected ':' after 'sat'");
            s.advanceWithNoScopeChanges();
            return ir.addSat({ parseSatClauses(s) });
        } else if (id == words["eq_reflexive"]) {
            return ir::Proof::makeEqualityReflexive();
        } else if (id == words["eq_transitive"]) {
            return ir::Proof::makeEqualityTransitive();
        } else if (id == words["load_store"]) {
            return ir::Proof::makeLoadStore();
        } else if (id == words["skip_store"]) {
            return ir::Proof::makeSkipStore();
        } else if (id == words["phi_enumerate"]) {
            return ir::Proof::makePhiEnumerate();
        } else if (id == words["phi_exclusivity"]) {
            return ir::Proof::makePhiExclusivity();
        } else if (id == words["phi_activate"]) {
            return ir::Proof::makePhiActivate();
        } else if (id == words["phi_active_backward"]) {
            return ir::Proof::makePhiActiveBackward();
        } else if (id == words["phi_load"]) {
            return ir::Proof::makePhiLoad();
        } else if (id == words["jump_active_forward"]) {
            return ir::Proof::makeJumpActiveForward();
        } else if (id == words["branch_active_forward"]) {
            return ir::Proof::makeBranchActiveForward();
        } else if (id == words["branch_decision"]) {
            return ir::Proof::makeBranchDecision();
        } else {
            s.error("Unknown tactic");
        }
    }

    std::vector<ir::Theorem> parseSatClauses(TokenStream s) {
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
    ir::Theorem parseSatClause(TokenStream& s) {
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
    std::vector<ir::Bool> parseConnectiveOperands(TokenStream& s, Word connective, Parse parseOperand) {
        std::vector<ir::Bool> operands { sortCast<ir::Bool>(parseOperand(s)) };
        while (s.tokKind() == TokenKind::Identifier && s.tok().word() == connective) {
            s.advance();
            operands.push_back(sortCast<ir::Bool>(parseOperand(s)));
        }
        return operands;
    }

    ir::Expr parseOrExpr(TokenStream& s) {
        auto operands = parseConnectiveOperands(s, words["or"], [this](TokenStream& s) { return parseAndExpr(s); });
        if (operands.size() == 1)
            return operands.front();
        return ir.addOr(operands);
    }

    ir::Expr parseAndExpr(TokenStream& s) {
        auto operands = parseConnectiveOperands(s, words["and"], [this](TokenStream& s) { return parseEqualityExpr(s); });
        if (operands.size() == 1)
            return operands.front();
        return ir.addAnd(operands);
    }

    ir::Expr parseEqualityExpr(TokenStream& s) {
        ir::Expr left = parseUnaryExpr(s);
        if (s.tokKind() == TokenKind::Equal) {
            s.advance();
            ir::Expr right = parseUnaryExpr(s);
            return ir.addEquality({ left, right });
        } else if (s.tokKind() == TokenKind::ExclaimEqual) {
            s.advance();
            ir::Expr right = parseUnaryExpr(s);
            return !ir.addEquality({ left, right });
        } else {
            return left;
        }
    }

    ir::Expr parseUnaryExpr(TokenStream& s) {
        if (s.tokKind() == TokenKind::Exclaim) {
            s.advance();
            return !sortCast<ir::Bool>(parseUnaryExpr(s));
        } else {
            return parsePostfixExpr(s);
        }
    }

    ir::Expr parsePostfixExpr(TokenStream& s) {
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
    ir::Expr parseLoad(TokenStream& s, ir::MemoryLoc loc) {
        if (s.tokKind() != TokenKind::LabelName)
            s.error("Expected label name after load");
        std::optional<ir::Sort> sort = ir.scalarSort(loc);
        if (!sort.has_value())
            s.error("The loaded location must be proven scalar before it is loaded");
        ir::CodePos pos = getLabel(s);
        s.advance();
        return ir.addLoad(*sort, { loc, pos });
    }

    ir::Expr parsePrimaryExpression(TokenStream& s) {
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
                ir::CodePos parentPos = getLabel(s);
                s.advance();
                if (ir.opcodeAt(labelPos) != ir::Opcode::Phi)
                    s.error("'.from' requires a phi instruction");
                ir::PhiParentList parents = ir.parents(labelPos);
                for (int_t i = 0; i < parents.size(); i++) {
                    ir::PhiParent parent = parents.at(i);
                    if (ir.parentPosition(parent) == parentPos)
                        return ir::Expr::makeParentEdgeTaken(parent);
                }
                s.error("Label is not a parent of the referenced phi");
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
                return ir::Expr::makeBooleanLiteral(false);
            } else if (id == words["true"]) {
                return ir::Expr::makeBooleanLiteral(true);
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
    ir::CodePos getLabel(TokenStream& s) {
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
    ir::CodePos getLabelForInstruction(TokenStream& s, ir::CodePos instruction, uint32_t index) {
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

    void defineLabel(Word name, ir::CodePos pos) {
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
                ir.setParent(use.instruction, use.index, pos);
                break;
            default:
                VERIFY_NOT_REACHED();
            }
        }
    }

    ir::Function& ir;
    LookupTable<ir::Theorem> theorems;
    LookupTable<LabelInfo> labels;
    LookupTable<ir::Expr> locals;
    std::vector<UnresolvedLabel> unresolvedLabels;
};

ParsedFunction parse(const char* source) {
    Lexer lexer;
    lexer.lex(source);
    ParsedFunction result;

    FunctionParser parser { result.function };
    auto s = TokenStream::makeRoot(lexer.tokens.data());
    VERIFY(s.tokKind() == TokenKind::BeginScope);
    s.advance();
    while (s.tokKind() == TokenKind::ContinueScope)
        s.advance();
    parser.parse(s);

    result.parameterNames.resize(result.function.parameterCount());
    parser.locals.forEachEntry([&result, &lexer](Word name, ir::Expr expr) {
        if (expr.kind() == ir::ExprKind::FunctionParameter)
            result.parameterNames[expr.id()] = lexer.wordTable.view(name);
    });
    parser.labels.forEachEntry([&result, &lexer](Word name, FunctionParser::LabelInfo info) {
        result.labels.emplace_back(lexer.wordTable.view(name), info.codePos());
    });

    return result;
}

static ir::Function parseForTest(const char* source) {
    return parse(source).function;
}

TEST(VerifyLanguage, ParseFunctionDefinition) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    store $a <- $b
    store $a <- $b
    store $a <- $b
    store $a <- $b
    store $a <- $b
)");
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.here().id(), 5);
}

TEST(VerifyLanguage, ParseParameterSorts) {
    ir::Function fn = parseForTest(R"(
fn #test($a: bool, $b, $c: memory_decl):
    nop
)");
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.sortOf(ir::Expr(ir::ExprKind::FunctionParameter, 0)), ir::Sort::Bool);
    // A parameter without a sort is a memory location
    EXPECT_EQ(fn.sortOf(ir::Expr(ir::ExprKind::FunctionParameter, 1)), ir::Sort::MemoryLoc);
    EXPECT_EQ(fn.sortOf(ir::Expr(ir::ExprKind::FunctionParameter, 2)), ir::Sort::MemoryDecl);

    EXPECT_THROW(parseForTest(R"(
fn #test($a: not_a_sort):
    nop
)"),
        ParserException);
}

TEST(VerifyLanguage, ParseNop) {
    ir::Function fn = parseForTest(R"(
fn #test():
@first:
    nop
@second:
    nop
)");
    // A nop occupies a code position without doing anything
    EXPECT_EQ(fn.here().id(), 2);
    EXPECT_EQ(fn.opcodeAt(ir::CodePos(0)), ir::Opcode::Nop);
    EXPECT_EQ(fn.opcodeAt(ir::CodePos(1)), ir::Opcode::Nop);
}

TEST(VerifyLanguage, ParseNames) {
    ParsedFunction parsed = parse(R"(
fn #test($a, $b):
@entry:
    jump @exit
@exit:
    nop
)");
    EXPECT_EQ(parsed.parameterNames, (std::vector<std::string> { "a", "b" }));
    // Label order is highly volatile due the hash map, nomalize it using sort.
    std::ranges::sort(parsed.labels, std::less(), [](std::pair<std::string, ir::CodePos> pair) { return pair.second.id(); });
    EXPECT_EQ(parsed.labels,
        (std::vector<std::pair<std::string, ir::CodePos>> {
            { "entry", ir::CodePos(0) },
            { "exit", ir::CodePos(1) } }));
}

TEST(VerifyLanguage, ParseStore) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    store $a <- $b
)");
    EXPECT_EQ(fn.parameterCount(), 2);
    EXPECT_EQ(fn.here().id(), 1);
    ir::CodePos storePos(0);
    EXPECT_EQ(fn.getStore(storePos).loc, ir::Expr(ir::ExprKind::FunctionParameter, 0));
    EXPECT_EQ(fn.getStore(storePos).value, ir::Expr(ir::ExprKind::FunctionParameter, 1));
}

TEST(VerifyLanguage, ParseBranchAndPhi) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
@entry:
    jump @before
@before:
    phi @entry, @branch
@branch:
    branch $a = $b, @before, @after
@after:
    phi @branch
)");
    EXPECT_EQ(fn.parameterCount(), 2);
    EXPECT_EQ(fn.here().id(), 4);
    ir::CodePos jumpPos(0);
    ir::CodePos phi1Pos(1);
    ir::CodePos branchPos(2);
    ir::CodePos phi2Pos(3);

    EXPECT_EQ(fn.getJump(jumpPos).target, phi1Pos);

    EXPECT_EQ(fn.parents(phi1Pos).size(), 2);
    EXPECT_EQ(fn.parentPosition(fn.parents(phi1Pos).at(0)), jumpPos);
    EXPECT_EQ(fn.parentPosition(fn.parents(phi1Pos).at(1)), branchPos);

    EXPECT_EQ(fn.getEquality(fn.getBranch(branchPos).cond).left, ir::Expr(ir::ExprKind::FunctionParameter, 0));
    EXPECT_EQ(fn.getEquality(fn.getBranch(branchPos).cond).right, ir::Expr(ir::ExprKind::FunctionParameter, 1));
    EXPECT_EQ(fn.getBranch(branchPos).ifTrue, phi1Pos);
    EXPECT_EQ(fn.getBranch(branchPos).ifFalse, phi2Pos);

    EXPECT_EQ(fn.parents(phi2Pos).size(), 1);
    EXPECT_EQ(fn.parentPosition(fn.parents(phi2Pos).at(0)), branchPos);
}

TEST(VerifyLanguage, ParseMultilineExpression) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    store $a <- $b = $c
    store $a <- $b
        = $c
    store $a <- $b =
        $c
    store $a
        <- $b = $c
    store
        $a <- $b = $c
    store
        $a
        <-
            $b
        =
            $c
)");
    auto testExpr = [&fn](ir::CodePos pos) {
        EXPECT_EQ(fn.getStore(pos).loc, ir::Expr(ir::ExprKind::FunctionParameter, 0));
        EXPECT_EQ(fn.getEquality((ir::Bool)fn.getStore(pos).value).left, ir::Expr(ir::ExprKind::FunctionParameter, 1));
        EXPECT_EQ(fn.getEquality((ir::Bool)fn.getStore(pos).value).right, ir::Expr(ir::ExprKind::FunctionParameter, 2));
    };
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.here().id(), 6);
    testExpr(ir::CodePos(0));
    testExpr(ir::CodePos(1));
    testExpr(ir::CodePos(2));
    testExpr(ir::CodePos(3));
    testExpr(ir::CodePos(4));
    testExpr(ir::CodePos(5));
}

TEST(VerifyLanguage, ParseConnectivePrecedence) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c, $d, $e, $f):
    store $a <- $a = $b and $c = $d or $e = $f
    store $a <- ($a = $b and $c = $d) or ($e = $f)
    store $a <- $a = $b and ($c = $d or $e = $f)
)");
    // 'or' binds weakest, so the first two stores hold the same expression
    ir::Bool first = (ir::Bool)fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(first.kind(), ir::ExprKind::Or);
    EXPECT_EQ((ir::Bool)fn.getStore(ir::CodePos(1)).value, first);

    auto disjuncts = fn.view(fn.getOr(first).operands);
    EXPECT_EQ(disjuncts.size(), 2);
    EXPECT_EQ(disjuncts[0].kind(), ir::ExprKind::And);
    EXPECT_EQ(disjuncts[1].kind(), ir::ExprKind::Equality);

    // The operands of the 'and' are the equalities, which bind strongest
    auto conjuncts = fn.view(fn.getAnd((ir::Bool)disjuncts[0]).operands);
    EXPECT_EQ(conjuncts.size(), 2);
    EXPECT_EQ(conjuncts[0].kind(), ir::ExprKind::Equality);
    EXPECT_EQ(conjuncts[1].kind(), ir::ExprKind::Equality);

    // Parentheses give the 'or' the higher precedence instead
    ir::Bool grouped = (ir::Bool)fn.getStore(ir::CodePos(2)).value;
    EXPECT_EQ(grouped.kind(), ir::ExprKind::And);
    EXPECT_EQ(fn.view(fn.getAnd(grouped).operands)[1].kind(), ir::ExprKind::Or);
}

TEST(VerifyLanguage, ParseConnectiveAssociativity) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    store $a <- $a = $a or $b = $b or $c = $c
    store $a <- ($a = $a or $b = $b) or $c = $c
    store $a <- $a = $a or ($b = $b or $c = $c)
    store $a <- $a = $a or !($b = $b or $c = $c)
)");
    // A chain of the same connective is one expression, however it is grouped
    ir::Bool chain = (ir::Bool)fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(fn.view(fn.getOr(chain).operands).size(), 3);
    EXPECT_EQ((ir::Bool)fn.getStore(ir::CodePos(1)).value, chain);
    EXPECT_EQ((ir::Bool)fn.getStore(ir::CodePos(2)).value, chain);

    // A negated operand is not merged into the surrounding disjunction
    ir::Bool negated = (ir::Bool)fn.getStore(ir::CodePos(3)).value;
    EXPECT_NE(negated, chain);
    auto operands = fn.view(fn.getOr(negated).operands);
    EXPECT_EQ(operands.size(), 2);
    EXPECT_EQ(operands[1].kind(), ir::ExprKind::Or);
    EXPECT_EQ((uint32_t)operands[1].boolNegatedBit, 1u);
}

TEST(VerifyLanguage, ParseMultilineConnective) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    prove $a = $b
        or $b = $c
        or $a = $c by eq_transitive
    store $a <- $a = $b or $b = $c or $a = $c
)");
    // Continuation lines do not change the expression
    ir::Bool prop = fn.prop(ir::Theorem(0));
    EXPECT_EQ(prop.kind(), ir::ExprKind::Or);
    EXPECT_EQ(fn.view(fn.getOr(prop).operands).size(), 3);
    EXPECT_EQ(fn.proof(ir::Theorem(0)).tactic(), ir::Tactic::EqualityTransitive);
    EXPECT_EQ((ir::Bool)fn.getStore(ir::CodePos(0)).value, prop);
}

TEST(VerifyLanguage, ParseConnectiveInSatClause) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    prove true by sat:
        clause $a = $b or $b = $a by sorry
        clause $a = $a and $b = $b by eq_reflexive
)");
    auto& proof = fn.getSat(fn.proof(ir::Theorem(2)));
    EXPECT_EQ(proof.clauses.size(), 2);

    // 'by' ends the clause expression, it is not read as another operand
    EXPECT_EQ(fn.prop(proof.clauses[0]).kind(), ir::ExprKind::Or);
    EXPECT_EQ(fn.proof(proof.clauses[0]).tactic(), ir::Tactic::Sorry);
    EXPECT_EQ(fn.prop(proof.clauses[1]).kind(), ir::ExprKind::And);
    EXPECT_EQ(fn.proof(proof.clauses[1]).tactic(), ir::Tactic::EqualityReflexive);
}

TEST(VerifyLanguage, ParseTheorems) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    pre %a_eq_b: $a = $b
    prove %reflex: $a = $a by eq_reflexive
    prove %transitive: $a != $b by eq_transitive
    prove %sorry_thm: $a = $c by sorry
    prove $b = $c by load_store
)");
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.here().id(), 0);

    ir::Theorem preTheorem(0);
    EXPECT_EQ(fn.position(preTheorem), ir::CodePos(0));
    EXPECT_EQ(fn.proof(preTheorem).tactic(), ir::Tactic::Precondition);
    EXPECT_EQ(fn.getEquality(fn.prop(preTheorem)).left, ir::Expr(ir::ExprKind::FunctionParameter, 0));
    EXPECT_EQ(fn.getEquality(fn.prop(preTheorem)).right, ir::Expr(ir::ExprKind::FunctionParameter, 1));

    ir::Theorem reflexTheorem(1);
    EXPECT_EQ(fn.proof(reflexTheorem).tactic(), ir::Tactic::EqualityReflexive);

    ir::Theorem transitiveTheorem(2);
    EXPECT_EQ(fn.proof(transitiveTheorem).tactic(), ir::Tactic::EqualityTransitive);

    ir::Theorem sorryTheorem(3);
    EXPECT_EQ(fn.proof(sorryTheorem).tactic(), ir::Tactic::Sorry);

    ir::Theorem loadStoreTheorem(4);
    EXPECT_EQ(fn.proof(loadStoreTheorem).tactic(), ir::Tactic::LoadStore);
}

TEST(VerifyLanguage, ParseLoad) {
    // The sort of a load is not spelled out, it follows from the 'scalarType' theorem
    ir::Function fn = parseForTest(R"(
fn #test($x, $y):
@entry:
    pre %x_scalar: $x.type.memory_loc_scalar
    store $y <- $x.load@entry
)");
    EXPECT_EQ(fn.here().id(), 1);

    ir::Expr value = fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(value.kind(), ir::ExprKind::MemoryLocLoad);
    EXPECT_EQ(fn.sortOf(value), ir::Sort::MemoryLoc);
    EXPECT_EQ(fn.getLoad(value).loc, ir::Expr(ir::ExprKind::FunctionParameter, 0));

    // A load of a location that was never proven scalar cannot be given a sort
    EXPECT_THROW(parseForTest(R"(
fn #test($x, $y):
@entry:
    store $y <- $x.load@entry
)"),
        ParserException);
}

TEST(VerifyLanguage, ParseDuplicateTheorems) {
    // Theorems are uniqued by their proposition, so proving one twice is an error
    EXPECT_THROW(parseForTest(R"(
fn #test($a, $b):
    prove $a = $b by sorry
    prove $a = $b by load_store
)"),
        ParserException);

    // Preconditions share the same propositions as the other theorems
    EXPECT_THROW(parseForTest(R"(
fn #test($a, $b):
    pre %a_eq_b: $a = $b
    prove $a = $b by load_store
)"),
        ParserException);

    // A proposition and its negation are distinct
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    prove $a = $b by sorry
    prove $a != $b by sorry
)");
    EXPECT_EQ(fn.prop(ir::Theorem(0)), !fn.prop(ir::Theorem(1)));
}

TEST(VerifyLanguage, ParseSatProof) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    prove true by sat:
        clause $a = $a by eq_reflexive
        clause $b = $b by eq_reflexive
)");
    EXPECT_EQ(fn.parameterCount(), 2);
    EXPECT_EQ(fn.here().id(), 0);

    // Writing a clause down states a theorem of its own, which comes before the one it proves
    ir::Theorem satTheorem(2);
    EXPECT_EQ(fn.position(satTheorem), ir::CodePos(0));
    EXPECT_EQ(fn.proof(satTheorem).tactic(), ir::Tactic::Sat);
    EXPECT_EQ(fn.prop(satTheorem), ir::Expr::makeBooleanLiteral(true));
    auto& proof = fn.getSat(fn.proof(satTheorem));
    EXPECT_EQ(proof.clauses, (std::vector<ir::Theorem> { ir::Theorem(0), ir::Theorem(1) }));
    for (ir::Theorem clause : proof.clauses) {
        EXPECT_EQ(fn.position(clause), ir::CodePos(0));
        EXPECT_EQ(fn.prop(clause).kind(), ir::ExprKind::Equality);
        EXPECT_EQ(fn.proof(clause).tactic(), ir::Tactic::EqualityReflexive);
    }
}

TEST(VerifyLanguage, ParseSatProofOfStatedTheorem) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    prove %a_eq_b: $a = $b by sorry
    prove true by sat:
        clause %a_eq_b
        clause $b = $b by eq_reflexive
)");
    // Naming a theorem refers to it, it does not state a second one
    auto& proof = fn.getSat(fn.proof(ir::Theorem(2)));
    EXPECT_EQ(proof.clauses, (std::vector<ir::Theorem> { ir::Theorem(0), ir::Theorem(1) }));

    // A theorem has to be stated before a clause can name it
    EXPECT_THROW(parseForTest(R"(
fn #test($a, $b):
    prove true by sat:
        clause %a_eq_b
    prove %a_eq_b: $a = $b by sorry
)"),
        ParserException);

    // A proof cannot rest on the theorem it establishes
    EXPECT_THROW(parseForTest(R"(
fn #test($a, $b):
    prove %circular: $a = $b by sat:
        clause %circular
)"),
        ParserException);

    // Restating the proposition of a theorem is an error for a clause as well
    EXPECT_THROW(parseForTest(R"(
fn #test($a, $b):
    prove %a_eq_b: $a = $b by sorry
    prove true by sat:
        clause $a = $b by eq_reflexive
)"),
        ParserException);

    // The clauses are stated first, so a clause restating the proposition of the theorem
    // they prove is caught the same way round
    EXPECT_THROW(parseForTest(R"(
fn #test($a, $b):
    prove $a = $b by sat:
        clause $a = $b by eq_reflexive
)"),
        ParserException);
}

TEST(VerifyLanguage, ParseUniqueExpressions) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    store $a <- $b = $c
    store $a <- $b = $c
    store $a <- $c = $b
    store $a <- $b = $b
    store $a <- ($b = $c) = ($b = $c)
)");
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.here().id(), 5);

    // Writing the same expression twice must not create a second expression
    ir::Expr bEqC = fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(fn.getStore(ir::CodePos(1)).value, bEqC);

    // The operands and their order are part of the identity of the expression
    EXPECT_NE(fn.getStore(ir::CodePos(2)).value, bEqC);
    EXPECT_NE(fn.getStore(ir::CodePos(3)).value, bEqC);

    // Nested expressions are uniqued as well
    auto nested = fn.getEquality((ir::Bool)fn.getStore(ir::CodePos(4)).value);
    EXPECT_EQ(nested.left, bEqC);
    EXPECT_EQ(nested.right, bEqC);
}

TEST(VerifyLanguage, ParseUniqueCallArguments) {
    ir::Function fn = parseForTest(R"(
fn #test($f, $a, $b):
    call $f($a, $b)
    call $f($a, $b)
    call $f($b, $a)
    call $f()
    call $f()
)");
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.here().id(), 5);

    auto args = [&fn](uint32_t pos) { return fn.getCall(ir::CodePos(pos)).args; };
    auto sameList = [](ir::ExprList a, ir::ExprList b) {
        return a.m_offset == b.m_offset && a.m_size == b.m_size;
    };

    // Identical argument lists are shared between the calls
    EXPECT_EQ(args(0).size(), 2);
    EXPECT_TRUE(sameList(args(1), args(0)));
    EXPECT_FALSE(sameList(args(2), args(0)));

    EXPECT_EQ(args(3).size(), 0);
    EXPECT_TRUE(sameList(args(4), args(3)));
}

TEST(VerifyLanguage, ParseEqualityNegation) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    store $a <- $a = $b
    store $a <- $a != $b
)");
    EXPECT_EQ(fn.parameterCount(), 2);
    EXPECT_EQ(fn.here().id(), 2);

    ir::Bool equal = (ir::Bool)fn.getStore(ir::CodePos(0)).value;
    ir::Bool notEqual = (ir::Bool)fn.getStore(ir::CodePos(1)).value;

    // '!=' reuses the expression of '=' and only differs in the negation bit of the handle
    EXPECT_EQ(equal.kind(), ir::ExprKind::Equality);
    EXPECT_EQ(notEqual.kind(), ir::ExprKind::Equality);
    EXPECT_EQ(equal.id(), notEqual.id());
    EXPECT_EQ((uint32_t)equal.boolNegatedBit, 0u);
    EXPECT_EQ((uint32_t)notEqual.boolNegatedBit, 1u);
    EXPECT_NE(equal, notEqual);
    EXPECT_EQ(!equal, notEqual);
}

}