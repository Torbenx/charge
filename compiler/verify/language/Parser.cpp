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
        || c == '_' || c == ':';
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
            parent->token = token;
            parent->child = nullptr;
            parent->invalid = invalid;
            parent->advance();
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
                locals.insert(s.tok().word(), ir.addParameter(ir::Sort::MemoryLoc));
                s.advance();
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
        s.advanceWithNoScopeChanges();

        if (s.tokKind() != TokenKind::BeginScope)
            s.error("Expected function body");
        parseInstructions(s);
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
        ir.addPhi({ .parents = ir.makePhiParentList(parents) });
    }

    template<typename T>
    T sortCast(ir::Expr e) {
        return (T)e;
    }

    ir::Expr parseExpression(TokenStream& s) {
        return parseBinaryExpr(s);
    }

    template<typename T>
    T parseExpression(TokenStream& s) {
        return sortCast<T>(parseExpression(s));
    }

    ir::Expr parseBinaryExpr(TokenStream& s) {
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
            return !sortCast<ir::Bool>(parseUnaryExpr(s));
        } else {
            return parsePostfixExpr(s);
        }
    }

    ir::Expr parsePostfixExpr(TokenStream& s) {
        ir::Expr base = parsePrimaryExpression(s);
        while (s.tokKind() == TokenKind::Point) {
            s.advance();
            if (s.tokKind() == TokenKind::Identifier) {
                Word id = s.tok().word();
                s.advance();
                if (id == words["load"]) {
                    if (s.tokKind() != TokenKind::LabelName)
                        s.error("Expected label name after load");
                    base = ir.addLoad({ sortCast<ir::MemoryLoc>(base), getLabel(s) });
                    continue;
                } else {
                    s.error("Unexpected identifier in postfix expression");
                }
            } else {
                s.error("Unexpected token after '.'");
            }
        }
        return base;
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

ir::Function parseForTest(const char* source) {
    Lexer lexer;
    lexer.lex(source);
    ir::Function result;
    FunctionParser parser { result };
    auto s = TokenStream::makeRoot(lexer.tokens.data());
    VERIFY(s.tokKind() == TokenKind::BeginScope);
    s.advance();
    while (s.tokKind() == TokenKind::ContinueScope)
        s.advance();
    parser.parse(s);
    return result;
}

TEST(VerifyLanguage, ParseFunctionDefinition) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c)
    store $a <- $b
    store $a <- $b
    store $a <- $b
    store $a <- $b
    store $a <- $b
)");
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.here().id(), 5);
}

TEST(VerifyLanguage, ParseStore) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b)
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
fn #test($a, $b)
@entry
    jump @before
@before
    phi @entry, @branch
@branch
    branch $a = $b, @before, @after
@after
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
fn #test($a, $b, $c)
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

}