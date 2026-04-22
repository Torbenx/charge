#include <EnumTable.h>
#include <parse/Parser.h>
#include <sema/Context.h>

#include <gtest/gtest.h>

#include <bitset>
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
        if (tok == LexerToken::Identifier)
            return "x";
        else
            VERIFY_NOT_REACHED();
    }
    return spelling;
}

ReturnStatus SimpleParser::apply(SimpleOutput& output, RecoveryElement e) {
    if (e.isSkip()) {
        // If we are at EOS there is nothing to skip which we report as an error
        m_state.status = skipToken() == LexerToken::EOS ? ReturnStatus::UnhandledCase : ReturnStatus::Ready;
    } else {
        const char* sourcePos = sourcePosition();
        setSourcePosition(exampleString(e.insert()).data());
        parse(output, 1);
        setSourcePosition(sourcePos);
    }
    return status();
}

ReturnStatus SimpleParser::apply(const NoOutput& output, RecoveryElement e) {
    if (e.isSkip()) {
        // If we are at EOS there is nothing to skip which we report as an error
        m_state.status = skipToken() == LexerToken::EOS ? ReturnStatus::UnhandledCase : ReturnStatus::Ready;
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
        m_state.status = skipToken() == LexerToken::EOS ? ReturnStatus::UnhandledCase : ReturnStatus::Ready;
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
                token.setData1<DataKind::Word>(generated_identifier);
        }
    }
    return status();
}

static ReturnStatus applyExpand(auto& parser, auto& output, const RecoveryInstructions& instructions) {
    for (int_t i = 0; i < (int_t)instructions.skipTokens; i++) {
        if (auto status = parser.apply(output, RecoveryElement::skip1()); status != ReturnStatus::Ready)
            return status;
    }
    for (auto insert : instructions.insertTokens) {
        if (auto status = parser.apply(output, RecoveryElement::insert(insert)); status != ReturnStatus::Ready)
            return status;
    }
    return ReturnStatus::Ready;
}

ReturnStatus Parser::apply(sema::Context& output, const RecoveryInstructions& instructions) {
    return applyExpand(*this, output, instructions);
}

ReturnStatus SimpleParser::apply(SimpleOutput& output, const RecoveryInstructions& instructions) {
    return applyExpand(*this, output, instructions);
}

ReturnStatus SimpleParser::apply(const NoOutput& output, const RecoveryInstructions& instructions) {
    return applyExpand(*this, output, instructions);
}

static std::vector<RecoveryElement> generateCases(SimpleParser& parser) {
    if (parser.status() == ReturnStatus::EOS)
        return {};

    std::vector<RecoveryElement> cases;
    if (parser.skipToken() != LexerToken::EOS)
        cases.push_back(RecoveryElement::skip1());

    std::bitset<std::to_underlying(LexerToken::EOS)> consideredInserts;
    auto addInserts = [&cases, &consideredInserts](std::span<const LexerToken> tokens) {
        for (LexerToken tok : tokens) {
            if (consideredInserts.test(std::to_underlying(tok)))
                continue;
            consideredInserts.set(std::to_underlying(tok));
            cases.push_back(RecoveryElement::insert(tok));
        }
    };

    std::vector<State> statesToConsider = { parser.state() };
    do {
        State state = statesToConsider.back();
        statesToConsider.pop_back();
        auto thens = thenStates(state);
        statesToConsider.insert(statesToConsider.end(), thens.begin(), thens.end());

        if (state == State::AfterExpression) {
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
        } else if (state == State::Expression) {
            static constexpr std::array continuations = {
                LexerToken::Identifier
            };
            addInserts(continuations);
        } else if (state != State::Error) {
            addInserts(possibleTokens(state));
        }
    } while (!statesToConsider.empty());

    return cases;
}

struct RecoveryState {
    struct PruneCondition {
        uint32_t tokenLookAhead;
        uint32_t costTolerance;
    };
    static constexpr std::array PRUNE_CONDITIONS = {
        PruneCondition { .tokenLookAhead = 3, .costTolerance = 0 },
        PruneCondition { .tokenLookAhead = 2, .costTolerance = 2 },
        PruneCondition { .tokenLookAhead = 1, .costTolerance = 3 },
    };

    struct ErrorNode;

    struct RecoveryCase : RecoveryElement {
        std::unique_ptr<ErrorNode> nextError = nullptr;
    };

    // An error node is pruned when there exists a node that is at least PRUNE_TOKEN_REQUIREMENT
    // source tokens ahead and uses the same or fewer modifications. All the best nodes are stored
    // in a vector ordered by total advance.
    struct ErrorNode {
        uint32_t totalAdvancedTokens() const { return state.parsedTokens + totalSkippedTokens - totalInsertedTokens; }
        uint32_t modificationCost() const { return totalSkippedTokens * 2 + totalInsertedTokens * 3; }

        std::optional<ErrorNode*> parent;
        uint32_t totalSkippedTokens = 0;
        uint32_t totalInsertedTokens = 0;
        SavedParserState state;
        std::vector<RecoveryCase> cases;
    };

    struct ReturnElement {
        uint32_t totalAdvancedTokens() const { return state.parsedTokens + totalSkippedTokens - totalInsertedTokens; }
        uint32_t parsedSourceTokens() const { return state.parsedTokens - totalInsertedTokens; }

        uint32_t totalSkippedTokens = 0;
        uint32_t totalInsertedTokens = 0;
        SavedParserState state;
        RecoveryElement recovery;
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

    static std::vector<RecoveryCase> generateCases(SimpleParser& parser) {
        std::vector<RecoveryCase> result;
        auto in = parse::generateCases(parser);
        for (auto& c : in)
            result.push_back({ std::move(c) });
        return result;
    }

    void insertError(ErrorNode& child) {
        ErrorInfo info(child);
        auto lbIt = std::lower_bound(bestErrors.begin(), bestErrors.end(), info, CompareByTotalAdvance());
        VERIFY(lbIt != bestErrors.end());

        if (lbIt != bestErrors.begin() || lbIt->totalAdvancedTokens == info.totalAdvancedTokens) {
            auto testIt = lbIt->totalAdvancedTokens == info.totalAdvancedTokens ? lbIt : std::prev(lbIt);
            VERIFY(testIt->totalAdvancedTokens >= info.totalAdvancedTokens);
            if (testIt->modificationCost < info.modificationCost) {
                return;
            }
        }

        auto insertIt = bestErrors.insert(lbIt, info);
        auto newEnd = std::remove_if(std::next(insertIt), bestErrors.end(), [&info](const ErrorInfo& other) {
            return other.modificationCost > info.modificationCost;
        });
        bestErrors.erase(newEnd, bestErrors.end());
    }

    void grind(SimpleParser& parser) {
        while (!queue.empty()) {
            ErrorNode& parent = *queue.front();
            queue.pop_front();
            bool pruneNode = false;
            for (const auto& cond : PRUNE_CONDITIONS) {
                auto it = std::partition_point(bestErrors.begin(), bestErrors.end(), [&](const ErrorInfo& info) {
                    return info.totalAdvancedTokens >= parent.totalAdvancedTokens() + cond.tokenLookAhead;
                });
                if (it == bestErrors.begin())
                    continue;
                VERIFY(std::prev(it)->totalAdvancedTokens >= parent.totalAdvancedTokens() + cond.tokenLookAhead);
                if (std::prev(it)->modificationCost + cond.costTolerance <= parent.modificationCost()) {
                    pruneNode = true;
                    break;
                }
            }
            if (pruneNode)
                continue;

            for (auto& c : parent.cases) {
                parser.restore(parent.state);

                // if (c.isInsert())
                //     println("Inserting '{}' at \"{}\"", exampleString(c.insert()), parent.state.sourcePosition);
                // else
                //     println("Skipping 1 at \"{}\"", parent.state.sourcePosition);
                if (parser.apply(NoOutput(), c) != ReturnStatus::Ready) {
                    // if (c.isInsert())
                    //     println("Inserting '{}' failed: {}", exampleString(c.insert()), formatInternalErrorMessage({ parser.save(), parser.skipToken() }));
                    continue;
                }
                int_t prevParsedTokens = parser.parsedTokens();
                parser.parse(NoOutput());
                int_t validTokensUntilError = parser.parsedTokens() - prevParsedTokens;
                // if (parser.error())
                //     println("{}", formatInternalErrorMessage({ parser.save(), parser.skipToken() }));

                parser.restore(parent.state);
                VERIFY(parser.apply(NoOutput(), c) == ReturnStatus::Ready);
                parser.parse(NoOutput(), validTokensUntilError);

                c.nextError = std::make_unique<ErrorNode>(ErrorNode {
                    .parent = &parent,
                    .totalSkippedTokens = (uint32_t)(parent.totalSkippedTokens + c.skips()),
                    .totalInsertedTokens = (uint32_t)(parent.totalInsertedTokens + c.inserts()),
                    .state = parser.save(),
                    .cases = generateCases(parser) });
                insertError(*c.nextError);
                queue.push_back(&*c.nextError);
            }
        }
    }

    std::vector<ReturnElement> recoveryPath(ErrorNode& inNode) const {
        std::vector<ReturnElement> result;
        ErrorNode* node = &inNode;
        while (node->parent.has_value()) {
            ErrorNode& parent = *node->parent;
            auto it = std::ranges::find_if(parent.cases, [node](const RecoveryCase& c) {
                return c.nextError.get() == node;
            });
            VERIFY(it != parent.cases.end());
            result.push_back(ReturnElement {
                .totalSkippedTokens = parent.totalSkippedTokens,
                .totalInsertedTokens = parent.totalInsertedTokens,
                .state = parent.state,
                .recovery = *it });
            node = &parent;
        }
        std::ranges::reverse(result);
        return result;
    }

    static std::vector<std::vector<ReturnElement>> recover(SimpleParser& parser, const SavedParserState& errorState) {
        VERIFY(parser.parsedTokens() == 0);
        parser.parse(NoOutput(), errorState.parsedTokens);
        VERIFY(!parser.error());
        auto error = std::make_unique<ErrorNode>(ErrorNode {
            .parent = std::nullopt,
            .state = parser.save(),
            .cases = generateCases(parser) });
        RecoveryState state { .rootError = std::move(error) };
        state.bestErrors.push_back(*state.rootError);
        state.queue.push_back(&*state.rootError);
        state.grind(parser);

        std::vector<std::vector<ReturnElement>> result;
        for (const ErrorInfo& info : state.bestErrors) {
            if (!info.node->cases.empty())
                break;
            result.emplace_back(state.recoveryPath(*info.node));
        }
        return result;
    }
    static std::vector<std::vector<ReturnElement>> recover(std::string_view source, const SavedParserState& errorState) {
        SimpleParser parser(source.data());
        return recover(parser, errorState);
    }

    void checkInvariances() {
        for (int_t i = 1; i < (int_t)bestErrors.size(); i++) {
            VERIFY(bestErrors[i - 1].totalAdvancedTokens >= bestErrors[i].totalAdvancedTokens);
            VERIFY(bestErrors[i - 1].modificationCost >= bestErrors[i].modificationCost);
            if (bestErrors[i - 1].totalAdvancedTokens == bestErrors[i].totalAdvancedTokens)
                VERIFY(bestErrors[i - 1].modificationCost == bestErrors[i].modificationCost);
        }
    }

    std::vector<ErrorInfo> bestErrors = {};
    std::list<ErrorNode*> queue = {};
    std::unique_ptr<ErrorNode> rootError = nullptr;
};

struct RecoveryGroup {
    RecoveryInstructions recovery;
    uint32_t sourceTokenPosition;
    bool unanimousAndIsolated = false;
    SavedParserState preFirstErrorState;

    bool operator==(const RecoveryGroup& other) const {
        return sourceTokenPosition == other.sourceTokenPosition
            && recovery == other.recovery;
    }
};

std::vector<RecoveryGroup> buildGroups(std::span<RecoveryState::ReturnElement> in) {
    std::vector<RecoveryGroup> result;
    uint32_t parsedSourceTokens = 0;
    for (auto& element : in) {
        if (result.empty() || element.parsedSourceTokens() != parsedSourceTokens) {
            result.push_back({ .recovery = {},
                .sourceTokenPosition = element.totalAdvancedTokens(),
                .preFirstErrorState = std::move(element.state) });
            parsedSourceTokens = element.parsedSourceTokens();
        }

        // Note: Application of inserts and skips commutes
        if (element.recovery.isInsert())
            result.back().recovery.insertTokens.push_back(element.recovery.insert());
        else
            result.back().recovery.skipTokens += 1;
    }
    return result;
}

std::vector<RecoveredError> recoverAndAnalyze(std::string_view source, const SavedParserState& rootErrorState) {
    SimpleParser parser(source.data());
    auto ungroupedPaths = RecoveryState::recover(parser, rootErrorState);
    VERIFY(!ungroupedPaths.empty());
    std::vector<std::vector<RecoveryGroup>> paths;
    for (auto& path : ungroupedPaths)
        paths.emplace_back(buildGroups(path));

    using It = std::vector<RecoveryGroup>::iterator;
    std::vector<It> errorIts;
    for (auto& path : paths)
        errorIts.push_back(path.begin());

    // Find all the unanimous, isolated error
    for (;;) {
        if (errorIts.front() == paths.front().end())
            break;
        const RecoveryGroup& group = *errorIts.front();
        bool allEqual = true;
        bool anyAtEnd = false;
        for (int_t pathId = 1; pathId < (int_t)paths.size(); pathId++) {
            if (errorIts[pathId] == paths[pathId].end()) {
                anyAtEnd = true;
                break;
            }
            if (*errorIts[pathId] != group) {
                allEqual = false;
                break;
            }
        }
        if (anyAtEnd)
            break;
        if (!allEqual) {
            // Advance the min element
            auto minIt = std::ranges::min_element(errorIts, std::less(), [](It it) { return it->sourceTokenPosition; });
            VERIFY(minIt != errorIts.end());
            *minIt += 1;
            continue;
        }

        int_t maxPrevPos = std::numeric_limits<int_t>::lowest();
        int_t minNextPos = std::numeric_limits<int_t>::max();
        for (int_t pathId = 0; pathId < (int_t)paths.size(); pathId++) {
            const auto& path = paths[pathId];
            auto it = errorIts[pathId];
            if (it > path.begin())
                maxPrevPos = std::max<int_t>(maxPrevPos, std::prev(it)->sourceTokenPosition);
            if (it < std::prev(path.end()))
                minNextPos = std::min<int_t>(minNextPos, std::next(it)->sourceTokenPosition);
        }
        VERIFY(maxPrevPos < (int_t)group.sourceTokenPosition);
        VERIFY(minNextPos > (int_t)group.sourceTokenPosition);
        static constexpr int_t ISOLATION_REQUIREMENT = 3;
        bool isIsolated = (int_t)group.sourceTokenPosition - ISOLATION_REQUIREMENT >= maxPrevPos
            && (int_t)group.sourceTokenPosition + ISOLATION_REQUIREMENT <= minNextPos;

        for (auto& it : errorIts) {
            if (isIsolated)
                it->unanimousAndIsolated = true;
            it += 1;
        }
    }

    // TODO: Implement some kind of deterministic tie break. Maybe based on custering?
    auto minPathIt = std::ranges::min_element(paths, std::less(),
        [](const std::vector<RecoveryGroup>& path) { return path.size(); });
    auto& path = *minPathIt;
    std::vector<RecoveredError> result;
    for (RecoveryGroup& elem : path) {
        result.push_back(RecoveredError {
            Error::make(std::move(elem.preFirstErrorState)),
            elem.recovery,
            elem.unanimousAndIsolated,
        });
    }
    return result;
}

[[maybe_unused]] static std::string buildMockupString(std::string_view source, std::span<const RecoveryState::ReturnElement> path) {
    SimpleParser parser(source.data());
    std::string result;
    for (auto elem : path) {
        const char* prev = parser.sourcePosition();
        parser.parse(NoOutput(), (int_t)elem.state.parsedTokens - parser.parsedTokens());
        VERIFY(parser.status() == ReturnStatus::Ready);
        result += std::string_view(prev, parser.sourcePosition());
        if (elem.recovery.isInsert()) {
            result += exampleString(elem.recovery.insert());
            result.push_back(' ');
        }
        parser.apply(NoOutput(), elem.recovery);
        VERIFY(!parser.error());
    }
    const char* prev = parser.sourcePosition();
    parser.parse(NoOutput());
    VERIFY(parser.done());
    result += std::string_view(prev, parser.sourcePosition());
    return result;
}

TEST(Parse, RecoveryBasic) {
    std::string_view source = "fn f(): { return a + a a; }";
    SimpleParser parser(source.data());
    parser.parse(NoOutput());
    EXPECT_FALSE(parser.done());
    auto results = RecoveryState::recover(source, parser.save());
    ASSERT_EQ(results.size(), 1);
    auto& result = results.front();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.front().recovery, RecoveryElement::skip1());
}

TEST(Parse, RecoveryBasic2) {
    std::string_view source = "fn f(): { return (a + ); }";
    SimpleParser parser(source.data());
    parser.parse(NoOutput());
    EXPECT_FALSE(parser.done());
    auto results = RecoveryState::recover(source, parser.save());
    ASSERT_EQ(results.size(), 1);
    auto& result = results.front();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.front().recovery, RecoveryElement::insert(LexerToken::Identifier));
}

TEST(Parse, RecoveryBasic12) {
    std::string_view source = "fn f1(): { return a + a a; } fn f2(): { return (a + ); }";
    SimpleParser parser(source.data());
    parser.parse(NoOutput());
    EXPECT_FALSE(parser.done());
    auto results = RecoveryState::recover(source, parser.save());
    ASSERT_EQ(results.size(), 1);
    auto& result = results.front();
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].recovery, RecoveryElement::skip1());
    EXPECT_EQ(result[1].recovery, RecoveryElement::insert(LexerToken::Identifier));
}

TEST(Parse, RecoveryUnterminatedScope) {
    std::string_view source = "namespace n: { ";
    SimpleParser parser(source.data());
    parser.parse(NoOutput());
    EXPECT_FALSE(parser.done());
    auto results = RecoveryState::recover(source, parser.save());
    ASSERT_EQ(results.size(), 1);
    auto& result = results.front();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.front().recovery, RecoveryElement::insert(LexerToken::RightBrace));
}

TEST(Parse, RecoveryEOSinExpr) {
    std::string_view source = "fn f(): { (a + ";
    SimpleParser parser(source.data());
    parser.parse(NoOutput());
    EXPECT_FALSE(parser.done());
    auto results = RecoveryState::recover(source, parser.save());
    ASSERT_EQ(results.size(), 1);
    auto& result = results.front();
    ASSERT_EQ(result.size(), 4);
    EXPECT_EQ(result[0].recovery, RecoveryElement::insert(LexerToken::Identifier));
    EXPECT_EQ(result[1].recovery, RecoveryElement::insert(LexerToken::RightParen));
    EXPECT_EQ(result[2].recovery, RecoveryElement::insert(LexerToken::SemiColon));
    EXPECT_EQ(result[3].recovery, RecoveryElement::insert(LexerToken::RightBrace));
    auto mockup = buildMockupString(source, result);
    EXPECT_EQ(mockup, "fn f(): { (a + x ) ; } ");
}

TEST(Parse, RecoveryMissingFunctionParameters) {
    std::string_view source = "fn f: { }";
    SimpleParser parser(source.data());
    parser.parse(NoOutput());
    EXPECT_FALSE(parser.done());
    auto results = RecoveryState::recover(source, parser.save());
    ASSERT_EQ(results.size(), 1);
    auto& result = results.front();
    auto mockup = buildMockupString(source, result);
    EXPECT_EQ(mockup, "fn f( ) : { }");
}

}