#include <parse/parse_impl.h>

#include <gtest/gtest.h>

#include <list>

namespace parse::recovery {

enum class SyntaxCategory : uint8_t {
    Expression,
    Statement,
    ParameterList,
    Declaration,
};

template<typename E, typename T>
struct EnumTable {
private:
    static constexpr size_t N = std::to_underlying(E::COUNT);

    std::array<T, N> m_data;

    template<size_t... I>
    constexpr EnumTable(T defaultValue, std::index_sequence<I...>)
        : m_data { ((void)I, defaultValue)... } { }

public:
    struct Entry {
        E key;
        T value;
        constexpr Entry(E key, T value)
            : key(key), value(value) { }
    };
    constexpr EnumTable(T defaultValue, std::initializer_list<Entry> entries)
        : EnumTable(defaultValue, std::make_index_sequence<N>()) {
        std::array<bool, N> checkArray;
        checkArray.fill(false);
        for (const Entry& entry : entries) {
            auto val = std::to_underlying(entry.key);
            VERIFY(!checkArray[val]);
            checkArray[val] = true;
            m_data[val] = entry.value;
        }
    }

    constexpr EnumTable(std::initializer_list<Entry> entries)
        : EnumTable(T(), std::make_index_sequence<N>()) {
        std::array<bool, N> checkArray;
        checkArray.fill(false);
        for (const Entry& entry : entries) {
            auto val = std::to_underlying(entry.key);
            VERIFY(!checkArray[val]);
            checkArray[val] = true;
            m_data[val] = entry.value;
        }
        std::ranges::all_of(checkArray, [](bool b) { return b; });
    }

    constexpr T operator()(E e) const {
        auto i = std::to_underlying(e);
        assert(i >= 0 && i < N && "Index out of range");
        return m_data[i];
    }
};

constexpr EnumTable<ScopeKind, SyntaxCategory> containingSyntax {
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

const EnumTable<ScopeKind, std::vector<LexerToken>> endTokens {
    { ScopeKind::IfExpr, { LexerToken::EqualGreater } },
    { ScopeKind::IfExprOrStmt, { LexerToken::EqualGreater, LexerToken::Colon } },
    { ScopeKind::CompoundStmt, { LexerToken::RightBrace } },
    { ScopeKind::Paren, { LexerToken::RightParen } },
    { ScopeKind::ParenInImplExpr, { LexerToken::RightParen } },
    { ScopeKind::Square, { LexerToken::RightSquare } },
    { ScopeKind::Brace, { LexerToken::RightBrace } },
    { ScopeKind::BraceInImplExpr, { LexerToken::RightBrace } },
    { ScopeKind::LeftExpr,
        {
            LexerToken::RightParen,
            LexerToken::RightSquare,
            LexerToken::RightBrace,
            LexerToken::Comma,
            LexerToken::SemiColon,

            LexerToken::AmpAmpEqual,
            LexerToken::AmpEqual,
            LexerToken::Equal,
            LexerToken::ExclaimEqual,
            LexerToken::GreaterEqual,
            LexerToken::GreaterGreaterEqual,
            LexerToken::HatEqual,
            LexerToken::LessEqual,
            LexerToken::LessLessEqual,
            LexerToken::MinusEqual,
            LexerToken::PercentEqual,
            LexerToken::PlusEqual,
            LexerToken::SlashEqual,
            LexerToken::StarEqual,
            LexerToken::VertEqual,
            LexerToken::VertVertEqual,
        } },
    { ScopeKind::RightExpr,
        {
            LexerToken::RightParen,
            LexerToken::RightSquare,
            LexerToken::RightBrace,
            LexerToken::Comma,
            LexerToken::SemiColon,
        } },
    { ScopeKind::VariableType, { LexerToken::Equal, LexerToken::SemiColon, LexerToken::Comma, LexerToken::RightParen } },
    { ScopeKind::IfBranch, { LexerToken::SemiColon, LexerToken::RightBrace, LexerToken::Else } },
    { ScopeKind::ElseBranch, { LexerToken::SemiColon, LexerToken::RightBrace } },
    { ScopeKind::Parameter, { LexerToken::RightParen } },
    { ScopeKind::Namespace, { LexerToken::RightBrace } },
    { ScopeKind::FunctionBody, { LexerToken::RightBrace } },
    { ScopeKind::ReturnType, { LexerToken::Colon, LexerToken::SemiColon } },
    { ScopeKind::FunctionParameters, { LexerToken::RightParen } },
    { ScopeKind::Struct, { LexerToken::RightBrace } },
    { ScopeKind::Enum, { LexerToken::RightBrace } },
    { ScopeKind::BaseTypeExpr, { LexerToken::Equal, LexerToken::SemiColon } },
    { ScopeKind::TemplateParameters, { LexerToken::RightParen } },
    { ScopeKind::StructImplExpression, { LexerToken::Colon } },
    { ScopeKind::FunctionImplExpression, { LexerToken::LeftParen } },
    { ScopeKind::EnumImplExpression, { LexerToken::Colon } },
    { ScopeKind::GlobalImplExpression, { LexerToken::Colon, LexerToken::Equal, LexerToken::SemiColon } },
    { ScopeKind::GenericCategoryExpression, { LexerToken::Greater } },
};

struct RecoveryInstructions {
    uint32_t skipTokens = 0;
    uint32_t popScopes = 0;
    std::optional<State> continueState = std::nullopt;
    std::vector<ScopeKind> pushScopes = {};

    bool operator==(const RecoveryInstructions&) const = default;
};

void applyInstructions(Parser& parser, const RecoveryInstructions& c) {
    println("Skipping {} tokens", c.skipTokens);
    for (int_t i = 0; i < (int_t)c.skipTokens; i++)
        parser.lexToken();
    for (int_t i = 0; i < (int_t)c.popScopes; i++)
        parser.popScope();
    for (ScopeKind scope : c.pushScopes)
        parser.pushScope(scope);
    if (c.continueState.has_value())
        parser.setState(c.continueState.value());
}

std::vector<RecoveryInstructions> generateCases(Parser& parser) {
    if (parser.status() == ReturnStatus::EOS) {
        VERIFY(parser.checkFinalState());
        return {};
    }

    State state = parser.state();
    ScopeKind scope = parser.scopes().back();
    SyntaxCategory syntax = containingSyntax(scope);
    LexerToken token = parser.lexToken();
    if (token == LexerToken::EOS)
        return {};
    std::vector<RecoveryInstructions> cases;
    std::vector<LexerToken> skipped;
    cases.push_back({ .skipTokens = 1 });
    println("recovering at token {}", nameString(token));

    for (; token != LexerToken::EOS && skipped.size() < 10; skipped.push_back(token), token = parser.lexToken()) {
        if (std::ranges::contains(endTokens(scope), token)) {
            switch (syntax) {
            case SyntaxCategory::Expression: {
                if (state == State::AfterExpression || state == State::AfterImplExpression || state == State::MaybeDesignatedArgument) {
                    // after expression -> just skip the tokens
                    cases.push_back({ .skipTokens = (uint32_t)skipped.size() });
                } else {
                    // at expression -> scopes usually end after expressions
                    bool implExpr = state == State::ImplExpression || state == State::ImplAccessExpression;
                    cases.push_back({ .skipTokens = (uint32_t)skipped.size(), .continueState = implExpr ? State::AfterImplExpression : State::AfterExpression });
                }
                break;
            }
            case SyntaxCategory::Statement: {
                VERIFY(token == LexerToken::RightBrace || token == LexerToken::SemiColon || token == LexerToken::Else);
                if (token == LexerToken::Else) {
                    VERIFY(scope == ScopeKind::IfBranch);
                    cases.push_back({ .skipTokens = (uint32_t)skipped.size(), .continueState = State::AfterStatement });
                } else {
                    cases.push_back({ .skipTokens = (uint32_t)(skipped.size() + 1), .popScopes = 1, .continueState = State::AfterStatement });
                }
                break;
            }
            case SyntaxCategory::ParameterList: {
                VERIFY(token == LexerToken::RightParen);
                // Skip tokens including the ')' and continue after the parameters
                // Pop a 'Parameter' scope if there is one.
                cases.push_back({ .skipTokens = (uint32_t)(skipped.size() + 1),
                    .popScopes = scope == ScopeKind::Parameter ? 1u : 0u,
                    .continueState = State::AfterParameters });
                break;
            }
            case SyntaxCategory::Declaration: {
                VERIFY(token == LexerToken::RightBrace);
                // Skip tokens including the '}' and continue after the declaration
                cases.push_back({ .skipTokens = (uint32_t)(skipped.size() + 1), .continueState = State::AfterDeclaration });
                break;
            }
            default:
                break;
            }

            // Do not continue after the inner most scope ends.
            break;
        }
    }

    return cases;
}

}

namespace parse {

struct RecoveryState {
    struct ErrorNode;

    struct RecoveryCase : recovery::RecoveryInstructions {
        std::unique_ptr<ErrorNode> nextError = nullptr;
    };

    // An error node is considered 'bad' when there exists a node with greater advance that
    // skips fewer tokens. All the 'good' nodes are stored in a vector ordered by total advance.
    // The 'bad depth' is number of consecutive error nodes that are bad. Nodes at a bad depth
    // greater than maxBadDepth are pruned.
    struct ErrorNode {
        uint32_t totalAdvancedTokens() const { return totalSkippedTokens + totalParsedTokens; }

        std::optional<ErrorNode*> parent;
        uint32_t totalSkippedTokens = 0;
        uint32_t totalParsedTokens = 0;
        uint32_t badDepth = 0;
        SavedState state;
        std::vector<RecoveryCase> cases;
    };

    struct ErrorInfo {
        uint32_t totalAdvancedTokens() const { return totalSkippedTokens + totalParsedTokens; }

        ErrorInfo(ErrorNode& node)
            : totalSkippedTokens(node.totalSkippedTokens)
            , totalParsedTokens(node.totalParsedTokens)
            , node(&node) { }

        uint32_t totalSkippedTokens = 0;
        uint32_t totalParsedTokens = 0;
        ErrorNode* node = nullptr;

        bool operator==(const ErrorInfo&) const = default;
    };

    struct CompareByTotalAdvance {
        bool operator()(const ErrorInfo& l, const ErrorInfo& r) const {
            return l.totalAdvancedTokens() > r.totalAdvancedTokens();
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

    static std::vector<RecoveryCase> generateCases(Parser& parser) {
        std::vector<RecoveryCase> result;
        auto in = recovery::generateCases(parser);
        for (auto& c : in)
            result.push_back({ std::move(c) });
        return result;
    }

    void insertError(ErrorNode& child) {
        auto dump = [&] {
            for (auto& x : goodErrors) {
                print("(adv={} skip={}) ", x.totalAdvancedTokens(), x.totalSkippedTokens);
            }
            println("");
        };

        ErrorInfo info(child);
        print("Inserting (adv={} skip={}) into ", info.totalAdvancedTokens(), info.totalSkippedTokens);
        dump();
        auto lbIt = std::lower_bound(goodErrors.begin(), goodErrors.end(), info, CompareByTotalAdvance());
        if (lbIt == goodErrors.end()) {
            if (goodErrors.empty() || goodErrors.back().totalSkippedTokens >= info.totalSkippedTokens)
                goodErrors.push_back(info);
            return;
        }

        if (lbIt != goodErrors.begin() || lbIt->totalAdvancedTokens() == info.totalAdvancedTokens()) {
            auto testIt = lbIt->totalAdvancedTokens() == info.totalAdvancedTokens() ? lbIt : std::prev(lbIt);
            VERIFY(testIt->totalAdvancedTokens() >= info.totalAdvancedTokens());
            if (testIt->totalSkippedTokens < info.totalSkippedTokens) {
                // child should not have children of its own jet, so no need to use markBad().
                VERIFY(child.parent.has_value());
                child.badDepth = child.parent->badDepth + 1;
                return;
            }
        }

        auto insertIt = goodErrors.insert(lbIt, info);
        auto newEnd = std::stable_partition(std::next(insertIt), goodErrors.end(), [&info](const ErrorInfo& other) {
            return other.totalSkippedTokens <= info.totalSkippedTokens;
        });
        for (const auto& other : std::ranges::subrange(newEnd, goodErrors.end()))
            markBad(*other.node);
        goodErrors.erase(newEnd, goodErrors.end());
    }

    void grind(Parser& parser) {
        while (!queue.empty()) {
            ErrorNode& parent = *queue.front();
            queue.pop_front();
            if (parent.badDepth >= 2)
                continue;
            for (auto& c : parent.cases) {
                parser.restore(parent.state);
                recovery::applyInstructions(parser, c);
                SimpleOutput output;
                println("parsing \"{}\"", parser.save().sourcePosition);
                parser.parse(output);

                if (output.tokenBuffer.tokens.empty()) {
                    // Made no progresss
                    continue;
                }
                c.nextError = std::make_unique<ErrorNode>(ErrorNode {
                    .parent = &parent,
                    .totalSkippedTokens = parent.totalSkippedTokens + c.skipTokens,
                    .totalParsedTokens = (uint32_t)(parent.totalParsedTokens + output.tokenBuffer.tokens.size()),
                    .state = parser.save(),
                    .cases = generateCases(parser) });
                insertError(*c.nextError);
                checkInvariances();
                queue.push_back(&*c.nextError);
            }
        }
    }

    std::vector<recovery::RecoveryInstructions> recoveryPath() const {
        VERIFY(!goodErrors.empty());
        std::vector<recovery::RecoveryInstructions> result;
        ErrorNode* node = goodErrors.front().node;
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

    static std::vector<recovery::RecoveryInstructions> recover(Parser& parser) {
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
            VERIFY(goodErrors[i - 1].totalAdvancedTokens() >= goodErrors[i].totalAdvancedTokens());
            VERIFY(goodErrors[i - 1].totalSkippedTokens >= goodErrors[i].totalSkippedTokens);
            if (goodErrors[i - 1].totalAdvancedTokens() == goodErrors[i].totalAdvancedTokens())
                VERIFY(goodErrors[i - 1].totalSkippedTokens == goodErrors[i].totalSkippedTokens);
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
    Parser parser(source.data());
    SimpleOutput output;
    parser.parse(output);
    EXPECT_FALSE(parser.done());
    auto result = RecoveryState::recover(parser);
    EXPECT_EQ(result.size(), 1);
    recovery::RecoveryInstructions expected = { .skipTokens = 1 };
    EXPECT_EQ(result.front(), expected);
}

TEST(Parse, RecoveryBasic2) {
    std::string_view source = "fn f(): { return (a + ); }";
    Parser parser(source.data());
    SimpleOutput output;
    parser.parse(output);
    EXPECT_FALSE(parser.done());
    auto result = RecoveryState::recover(parser);
    EXPECT_EQ(result.size(), 1);
    recovery::RecoveryInstructions expected = { .skipTokens = 0, .continueState = State::AfterExpression };
    EXPECT_EQ(result.front(), expected);
}

TEST(Parse, RecoveryBasic12) {
    std::string_view source = "fn f1(): { return a + a a; } fn f2(): { return (a + ); }";
    Parser parser(source.data());
    SimpleOutput output;
    parser.parse(output);
    EXPECT_FALSE(parser.done());
    auto result = RecoveryState::recover(parser);
    EXPECT_EQ(result.size(), 2);
    recovery::RecoveryInstructions expected1 = { .skipTokens = 1 };
    recovery::RecoveryInstructions expected2 = { .skipTokens = 0, .continueState = State::AfterExpression };
    EXPECT_EQ(result[0], expected1);
    EXPECT_EQ(result[1], expected2);
}

}