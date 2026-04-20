#include <parse/Parser.h>
#include <sema/Context.h>
#include <EnumTable.h>

#include <gtest/gtest.h>

#include <list>

namespace parse {

//! Represents either a token insertion or skipping a single token
struct RecoveryElement {
    static RecoveryElement skip1() { return { std::nullopt }; }
    static RecoveryElement insert(LexerToken token) { return { token }; }

    bool isInsert() const { return m_token.has_value(); }
    bool isSkip() const { return !isInsert(); }
    LexerToken insert() const { return m_token.value(); }
    int_t inserts() const { return isInsert() ? 1 : 0; }
    int_t skips() const { return isInsert() ? 0 : 1; }

    bool operator==(const RecoveryElement&) const = default;

    std::optional<LexerToken> m_token;
};

static std::string_view exampleString(LexerToken tok) {
    std::string_view spelling = fixedSpelling(tok);
    if (spelling.empty()) {
        VERIFY(tok == LexerToken::Identifier);
        return "x";
    }
    return spelling;
}

ReturnStatus SimpleParser::apply(SimpleOutput& output, RecoveryElement e) {
    if (e.isSkip()) {
        // If we are at EOS there is nothing to skip which we report as an error
        m_state.status = lexToken() == LexerToken::EOS ? ReturnStatus::UnhandledCase : ReturnStatus::Ready;
    } else {
        const char* sourcePos = sourcePosition();
        setSourcePosition(exampleString(e.insert()).data());
        parse(output, 1);
        setSourcePosition(sourcePos);
    }
    return status();
}

ReturnStatus Parser::apply(sema::Context& context, RecoveryElement e) {
    if (e.isSkip()) {
        // If we are at EOS there is nothing to skip which we report as an error
        m_state.status = lexToken() == LexerToken::EOS ? ReturnStatus::UnhandledCase : ReturnStatus::Ready;
    } else {
        // Note: The recovery algorithm never inserts an argument name because in case like
        //       (: expr) dropping the ':' is cheaper than inserting an identifier.
        //       So we should not have to update any identifiers the argumentBuffer.
        int_t oldTokenCount = context.tokenBuffer.tokens.size();
        SourceLocation loc = location(context);
        const char* sourcePos = sourcePosition();
        m_state.sourcePosition = exampleString(e.insert()).data();
        parse(context, 1);
        m_state.sourcePosition = sourcePos;
        for (int_t i = oldTokenCount; i < context.tokenBuffer.tokens.size(); i++) {
            TokenInfo& token = context.tokenBuffer.tokens[i];
            token.setLocation(loc);
            // Mark generated identifiers
            if (lexerToken(token.kind()) == LexerToken::Identifier)
                token.setData1<DataKind::Word>(unresolved_identifier);
        }
    }
    return status();
}

enum class SyntaxCategory : uint8_t {
    Expression,
    Statement,
    ParameterList,
    Declaration,
};

static constexpr EnumTable<ScopeKind, SyntaxCategory> containingSyntax {
    { ScopeKind::IfExpr, SyntaxCategory::Expression },
    { ScopeKind::IfExprOrStmt, SyntaxCategory::Expression },
    { ScopeKind::CompoundStmt, SyntaxCategory::Statement },
    { ScopeKind::Paren, SyntaxCategory::Expression },
    { ScopeKind::ParenInImplExpr, SyntaxCategory::Expression },
    { ScopeKind::Square, SyntaxCategory::Expression },
    { ScopeKind::Brace, SyntaxCategory::Expression },
    { ScopeKind::BraceInImplExpr, SyntaxCategory::Expression },
    { ScopeKind::LeftExpr, SyntaxCategory::Expression },
    { ScopeKind::RightExpr, SyntaxCategory::Expression },
    { ScopeKind::VariableType, SyntaxCategory::Expression },
    { ScopeKind::IfBranch, SyntaxCategory::Statement },
    { ScopeKind::ElseBranch, SyntaxCategory::Statement },
    { ScopeKind::Parameter, SyntaxCategory::ParameterList },
    { ScopeKind::Namespace, SyntaxCategory::Declaration },
    { ScopeKind::FunctionBody, SyntaxCategory::Statement },
    { ScopeKind::ReturnType, SyntaxCategory::Expression },
    { ScopeKind::FunctionParameters, SyntaxCategory::ParameterList },
    { ScopeKind::Struct, SyntaxCategory::Declaration },
    { ScopeKind::Enum, SyntaxCategory::Declaration },
    { ScopeKind::BaseTypeExpr, SyntaxCategory::Expression },
    { ScopeKind::TemplateParameters, SyntaxCategory::ParameterList },
    { ScopeKind::StructImplExpression, SyntaxCategory::Expression },
    { ScopeKind::FunctionImplExpression, SyntaxCategory::Expression },
    { ScopeKind::EnumImplExpression, SyntaxCategory::Expression },
    { ScopeKind::GlobalImplExpression, SyntaxCategory::Expression },
    { ScopeKind::GenericCategoryExpression, SyntaxCategory::Expression },
};

static std::vector<RecoveryElement> generateCases(SimpleParser& parser) {
    if (parser.status() == ReturnStatus::EOS) {
        if (parser.checkFinalState())
            return {};
        else
            return { RecoveryElement::insert(LexerToken::RightBrace) };
    }

    State state = parser.state();
    ScopeKind scope = parser.scopes().back();
    SyntaxCategory syntax = containingSyntax(scope);
    LexerToken token = parser.lexToken();
    if (token == LexerToken::EOS)
        return {};
    std::vector<RecoveryElement> cases;
    std::vector<LexerToken> skipped;
    cases.push_back(RecoveryElement::skip1());

    auto addInsert = [&cases](LexerToken token) { cases.push_back(RecoveryElement::insert(token)); };
    auto addInserts = [&cases](std::span<const LexerToken> tokens) {
        for (LexerToken tok : tokens)
            cases.push_back(RecoveryElement::insert(tok));
    };

    switch (syntax) {
    case SyntaxCategory::Expression: {
        if (state == State::AfterExpression || state == State::AfterImplExpression || state == State::MaybeDesignatedArgument) {
            // after expression ->
            static constexpr std::array continuations = {
                LexerToken::RightParen,
                LexerToken::RightSquare,
                LexerToken::RightBrace,
                LexerToken::EqualGreater,
                LexerToken::Colon,
                LexerToken::SemiColon,
                LexerToken::Equal,
                LexerToken::Greater // For generic category expressions and stand in as a binary operator
            };
            addInserts(continuations);
        } else if (state == State::MemberAccess || state == State::StaticAccess) {
            // failed access -> insert an identifier
            addInsert(LexerToken::Identifier);
        } else {
            // at expression -> insert a primary expression
            addInsert(LexerToken::Identifier);
        }
        break;
    }
    case SyntaxCategory::Statement:
        // These scopes can never be at the top-most one when an error occours.
        break;
    case SyntaxCategory::ParameterList: {
        VERIFY(scope == ScopeKind::Parameter); // The other scopes can never be at the top when an error occours.
        addInserts(possibleTokens(state));
        break;
    }
    case SyntaxCategory::Declaration: {
        addInserts(possibleTokens(state));
        break;
    }
    default:
        VERIFY_NOT_REACHED();
    }

    return cases;
}

struct RecoveryState {
    struct ErrorNode;

    struct RecoveryCase : RecoveryElement {
        std::unique_ptr<ErrorNode> nextError = nullptr;
    };

    // An error node is considered 'bad' when there exists a node with greater advance that
    // requires fewer modifications. All the 'good' nodes are stored in a vector ordered by total
    // advance. The 'bad depth' is number of consecutive error nodes that are bad. Nodes at a bad
    // depth greater than maxBadDepth are pruned.
    struct ErrorNode {
        uint32_t totalAdvancedTokens() const { return totalSkippedTokens + totalParsedTokens - totalInsertedTokens; }
        uint32_t modificationCost() const { return totalSkippedTokens * 2 + totalInsertedTokens * 3; }

        std::optional<ErrorNode*> parent;
        uint32_t totalSkippedTokens = 0;
        uint32_t totalParsedTokens = 0;
        uint32_t totalInsertedTokens = 0;
        uint32_t badDepth = 0;
        SimpleParser::SavedState state;
        std::vector<RecoveryCase> cases;
    };

    struct ErrorInfo {
        ErrorInfo(ErrorNode& node)
            : totalAdvancedTokens(node.totalAdvancedTokens())
            , modificationCost(node.modificationCost())
            , node(&node) { }

        uint32_t totalAdvancedTokens = 0;
        uint32_t modificationCost = 0;
        ErrorNode* node = nullptr;

        bool operator==(const ErrorInfo&) const = default;
    };

    struct CompareByTotalAdvance {
        bool operator()(const ErrorInfo& l, const ErrorInfo& r) const {
            return l.totalAdvancedTokens > r.totalAdvancedTokens;
        }
    };

    static void markBad(ErrorNode& node, int_t newDepth = 1) {
        // Note: This does not take into account the bad depth of the parent node.
        //       TBD if this desirable for the pruning behavior.
        VERIFY(node.badDepth == newDepth - 1);
        node.badDepth = newDepth;
        for (auto& c : node.cases) {
            if (c.nextError != nullptr && c.nextError->badDepth > 0)
                markBad(*c.nextError, newDepth + 1);
        }
    }

    static std::vector<RecoveryCase> generateCases(SimpleParser& parser) {
        std::vector<RecoveryCase> result;
        auto in = parse::generateCases(parser);
        for (auto& c : in)
            result.push_back({ std::move(c) });
        return result;
    }

    void insertError(ErrorNode& child) {
        ErrorInfo info(child);
        auto lbIt = std::lower_bound(goodErrors.begin(), goodErrors.end(), info, CompareByTotalAdvance());
        if (lbIt == goodErrors.end()) {
            if (goodErrors.empty() || goodErrors.back().modificationCost >= info.modificationCost)
                goodErrors.push_back(info);
            return;
        }

        if (lbIt != goodErrors.begin() || lbIt->totalAdvancedTokens == info.totalAdvancedTokens) {
            auto testIt = lbIt->totalAdvancedTokens == info.totalAdvancedTokens ? lbIt : std::prev(lbIt);
            VERIFY(testIt->totalAdvancedTokens >= info.totalAdvancedTokens);
            if (testIt->modificationCost < info.modificationCost) {
                // child should not have children of its own jet, so no need to use markBad().
                VERIFY(child.parent.has_value());
                child.badDepth = child.parent->badDepth + 1;
                return;
            }
        }

        auto insertIt = goodErrors.insert(lbIt, info);
        auto newEnd = std::stable_partition(std::next(insertIt), goodErrors.end(), [&info](const ErrorInfo& other) {
            return other.modificationCost <= info.modificationCost;
        });
        for (const auto& other : std::ranges::subrange(newEnd, goodErrors.end()))
            markBad(*other.node);
        goodErrors.erase(newEnd, goodErrors.end());
    }

    void grind(SimpleParser& parser) {
        while (!queue.empty()) {
            ErrorNode& parent = *queue.front();
            queue.pop_front();
            if (parent.badDepth >= 2)
                continue;
            for (auto& c : parent.cases) {
                parser.restore(parent.state);
                SimpleOutput output;

                if (parser.apply(output, c) != ReturnStatus::Ready)
                    continue;
                parser.parse(output);

                c.nextError = std::make_unique<ErrorNode>(ErrorNode {
                    .parent = &parent,
                    .totalSkippedTokens = (uint32_t)(parent.totalSkippedTokens + c.skips()),
                    .totalParsedTokens = (uint32_t)(parent.totalParsedTokens + output.tokenBuffer.tokens.size()),
                    .totalInsertedTokens = (uint32_t)(parent.totalInsertedTokens + c.inserts()),
                    .state = parser.save(),
                    .cases = generateCases(parser) });
                insertError(*c.nextError);
                checkInvariances();
                queue.push_back(&*c.nextError);
            }
        }
    }

    std::vector<RecoveryElement> recoveryPath() const {
        VERIFY(!goodErrors.empty());
        return recoveryPath(*goodErrors.front().node);
    }

    std::vector<RecoveryElement> recoveryPath(ErrorNode& inNode) const {
        std::vector<RecoveryElement> result;
        ErrorNode* node = &inNode;
        while (node->parent.has_value()) {
            ErrorNode& parent = *node->parent;
            auto it = std::ranges::find_if(parent.cases, [node](const RecoveryCase& c) {
                return c.nextError.get() == node;
            });
            VERIFY(it != parent.cases.end());
            result.push_back(*it);
            node = &parent;
        }
        std::ranges::reverse(result);
        return result;
    }

    static std::vector<RecoveryElement> recover(SimpleParser& parser) {
        auto error = std::make_unique<ErrorNode>(ErrorNode {
            .parent = std::nullopt,
            .totalSkippedTokens = 0,
            .totalParsedTokens = 0,
            .state = parser.save(),
            .cases = generateCases(parser) });
        RecoveryState state { .rootError = std::move(error) };
        state.goodErrors.push_back(*state.rootError);
        state.queue.push_back(&*state.rootError);
        state.grind(parser);
        parser.restore(state.rootError->state);
        return state.recoveryPath();
    }

    void checkNodeInvariances(ErrorNode& node) {
        bool inGoodErrors = std::ranges::contains(goodErrors, ErrorInfo(node));
        VERIFY(inGoodErrors == (node.badDepth == 0));
        for (const auto& c : node.cases) {
            if (c.nextError != nullptr)
                checkNodeInvariances(*c.nextError);
        }
    }

    void checkInvariances() {
        checkNodeInvariances(*rootError);
        for (int_t i = 1; i < (int_t)goodErrors.size(); i++) {
            VERIFY(goodErrors[i - 1].totalAdvancedTokens >= goodErrors[i].totalAdvancedTokens);
            VERIFY(goodErrors[i - 1].modificationCost >= goodErrors[i].modificationCost);
            if (goodErrors[i - 1].totalAdvancedTokens == goodErrors[i].totalAdvancedTokens)
                VERIFY(goodErrors[i - 1].modificationCost == goodErrors[i].modificationCost);
        }
    }

    std::vector<ErrorInfo> goodErrors = {};
    std::list<ErrorNode*> queue = {};
    std::unique_ptr<ErrorNode> rootError = nullptr;
};

TEST(Parse, LexEOF) {
    std::string_view source = "a\nstatic +";
    Parser parser(source.data());
    EXPECT_EQ(parser.lexToken(), LexerToken::Identifier);
    EXPECT_EQ(parser.lexToken(), LexerToken::Static);
    EXPECT_EQ(parser.lexToken(), LexerToken::Plus);
    EXPECT_EQ(parser.lexToken(), LexerToken::EOS);
}

TEST(Parse, RecoveryBasic) {
    std::string_view source = "fn f(): { return a + a a; }";
    SimpleParser parser(source.data());
    SimpleOutput output;
    parser.parse(output);
    EXPECT_FALSE(parser.checkFinalState());
    auto result = RecoveryState::recover(parser);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.front(), RecoveryElement::skip1());
}

TEST(Parse, RecoveryBasic2) {
    std::string_view source = "fn f(): { return (a + ); }";
    SimpleParser parser(source.data());
    SimpleOutput output;
    parser.parse(output);
    EXPECT_FALSE(parser.checkFinalState());
    auto result = RecoveryState::recover(parser);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.front(), RecoveryElement::insert(LexerToken::Identifier));
}

TEST(Parse, RecoveryBasic12) {
    std::string_view source = "fn f1(): { return a + a a; } fn f2(): { return (a + ); }";
    SimpleParser parser(source.data());
    SimpleOutput output;
    parser.parse(output);
    EXPECT_FALSE(parser.checkFinalState());
    auto result = RecoveryState::recover(parser);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], RecoveryElement::skip1());
    EXPECT_EQ(result[1], RecoveryElement::insert(LexerToken::Identifier));
}

}