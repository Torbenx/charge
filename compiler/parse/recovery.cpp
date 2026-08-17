#include <EnumTable.h>
#include <parse/Parser.h>
#include <sema/Context.h>

#include <gtest/gtest.h>

#include <bitset>
#include <list>
#include <unordered_map>

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
        m_state.status = skipToken(output) == LexerToken::EOS ? ReturnStatus::UnhandledCase : ReturnStatus::Ready;
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
        m_state.status = skipToken(output) == LexerToken::EOS ? ReturnStatus::UnhandledCase : ReturnStatus::Ready;
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
        m_state.status = skipToken(context) == LexerToken::EOS ? ReturnStatus::UnhandledCase : ReturnStatus::Ready;
    } else {
        // Note: The recovery algorithm never inserts an argument name because in case like
        //       (: expr) dropping the ':' is cheaper than inserting an identifier.
        //       So we should not have to update any identifiers the argumentBuffer.
        advanceToToken(context);
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

static std::vector<RecoveryElement> generateCases(const SavedParserState& savedState) {
    if (savedState.status == ReturnStatus::EOS)
        return {};

    std::vector<RecoveryElement> cases;
    const char* pos = savedState.sourcePosition;
    if (lexToken(pos) != LexerToken::EOS)
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

    std::vector<State> statesToConsider = { savedState.state };
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
                LexerToken::Comma,
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

struct StateHandle {
    uint32_t m_id = limits::max;
    constexpr uint32_t id() const { return m_id; }
    bool operator==(const StateHandle&) const = default;
};

//! Everything that determines how the parser continues from a recovery state.
/*!
The parsed token count is not part of it since it also counts the tokens inserted on the way to the state.
*/
struct StateKey {
    const char* sourcePosition = nullptr;
    std::vector<ScopeKind> scopes;
    State state;
    ReturnStatus status;

    bool operator==(const StateKey&) const = default;
};

struct StateTable {
    struct StateData {
        StateKey key;
        //! Index of the source token at key.sourcePosition
        uint32_t sourceTokenIndex = 0;
    };

    struct InternResult {
        StateHandle handle;
        bool isNew;
    };

    InternResult intern(const SavedParserState& state, uint32_t sourceTokenIndex) {
        StateKey key {
            .sourcePosition = state.sourcePosition,
            .scopes = state.scopeBuffer,
            .state = state.state,
            .status = state.status,
        };
        auto [it, isNew] = m_ids.try_emplace(key, StateHandle { (uint32_t)m_states.size() });
        if (isNew)
            m_states.push_back({ .key = key, .sourceTokenIndex = sourceTokenIndex });
        else
            VERIFY(m_states[it->second.id()].sourceTokenIndex == sourceTokenIndex);
        return { it->second, isNew };
    }

    int_t size() const { return (int_t)m_states.size(); }
    StateData& operator[](StateHandle handle) { return m_states[handle.id()]; }
    const StateData& operator[](StateHandle handle) const { return m_states[handle.id()]; }

private:
    struct Hash {
        size_t operator()(const StateKey& key) const {
            size_t hash = 0;
            hash_combine(hash, std::hash<const char*>()(key.sourcePosition));
            for (ScopeKind scope : key.scopes)
                hash_combine(hash, std::to_underlying(scope));
            hash_combine(hash, std::to_underlying(key.state));
            hash_combine(hash, std::to_underlying(key.status));
            return hash;
        }
    };

    std::unordered_map<StateKey, StateHandle, Hash> m_ids;
    std::vector<StateData> m_states;
};

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

    // An error node is pruned by PRUNE_CONDITIONS when there exists a node that is at least tokenLookAhead
    // source tokens ahead and uses costTolarance or fewer modifications. All the best nodes are stored
    // in a vector ordered by total advance.
    struct ErrorNode {
        uint32_t totalAdvancedTokens() const { return sourceTokenIndex; }
        uint32_t modificationCost() const { return totalSkippedTokens * 2 + totalInsertedTokens * 3; }

        uint32_t parentNode;
        RecoveryElement recovery;
        //! Index of the source token at preNextRecoveryState.sourcePosition. Unlike the parsed
        //! token count this only depends on the source position and not on the path taken.
        uint32_t sourceTokenIndex = 0;
        uint32_t totalSkippedTokens = 0;
        uint32_t totalInsertedTokens = 0;
        SavedParserState preNextRecoveryState;
    };

    struct ReturnElement {
        uint32_t totalAdvancedTokens() const { return preRecoveryState.parsedTokens + totalSkippedTokens - totalInsertedTokens; }
        uint32_t parsedSourceTokens() const { return preRecoveryState.parsedTokens - totalInsertedTokens; }

        uint32_t totalSkippedTokens = 0;
        uint32_t totalInsertedTokens = 0;
        SavedParserState preRecoveryState;
        RecoveryElement recovery;
    };

    struct ErrorInfo {
        ErrorInfo(uint32_t nodeIdx, ErrorNode& node)
            : nodeIdx(nodeIdx)
            , totalAdvancedTokens(node.totalAdvancedTokens())
            , modificationCost(node.modificationCost()) { }

        uint32_t nodeIdx;
        uint32_t totalAdvancedTokens = 0;
        uint32_t modificationCost = 0;

        bool operator==(const ErrorInfo&) const = default;
    };

    struct CompareByTotalAdvance {
        bool operator()(const ErrorInfo& l, const ErrorInfo& r) const {
            return l.totalAdvancedTokens > r.totalAdvancedTokens;
        }
    };

    void insertNode(ErrorNode child) {
        VERIFY(child.sourceTokenIndex == child.preNextRecoveryState.parsedTokens + child.totalSkippedTokens - child.totalInsertedTokens);
        stateTable.intern(child.preNextRecoveryState, child.sourceTokenIndex);
        uint32_t idx = allNodes.size();
        ErrorInfo info(idx, child);
        allNodes.emplace_back(std::move(child));
        // dbgprint("inserting (adv={}, mod={}) into", info.totalAdvancedTokens, info.modificationCost);
        // for (auto i : bestErrors)
        //     dbgprint(" (adv={}, mod={})", i.totalAdvancedTokens, i.modificationCost);
        // dbgln("");
        auto lbIt = std::lower_bound(bestErrors.begin(), bestErrors.end(), info, CompareByTotalAdvance());
        if (lbIt == bestErrors.end()) {
            if (bestErrors.empty()) {
                bestErrors.push_back(info);
                return;
            }
            VERIFY(bestErrors.back().totalAdvancedTokens > info.totalAdvancedTokens);
            if (bestErrors.back().modificationCost >= info.modificationCost)
                bestErrors.push_back(info);
            return;
        }

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

    void makeNodes(uint32_t parentIdx, RecoveryElement recovery, uint32_t parentSourceTokenIndex, uint32_t totalSkippedTokens, uint32_t totalInsertedTokens, SimpleParser& parser, int_t validTokensUntilError) {
        uint32_t sourceTokenIndex = parentSourceTokenIndex + (uint32_t)recovery.skips();
        if (validTokensUntilError > 1) {
            parser.parse(NoOutput(), validTokensUntilError - 1);
            insertNode({
                .parentNode = parentIdx,
                .recovery = recovery,
                .sourceTokenIndex = sourceTokenIndex + (uint32_t)validTokensUntilError - 1,
                .totalSkippedTokens = totalSkippedTokens,
                .totalInsertedTokens = totalInsertedTokens,
                .preNextRecoveryState = parser.save(),
            });
            parser.parse(NoOutput(), 1);
        } else {
            parser.parse(NoOutput(), validTokensUntilError);
        }

        insertNode({
            .parentNode = parentIdx,
            .recovery = recovery,
            .sourceTokenIndex = sourceTokenIndex + (uint32_t)validTokensUntilError,
            .totalSkippedTokens = totalSkippedTokens,
            .totalInsertedTokens = totalInsertedTokens,
            .preNextRecoveryState = parser.save(),
        });
    }

    void grind(SimpleParser& parser) {
        while (nextNodeToProcessed < (int_t)allNodes.size()) {
            int_t parentIdx = nextNodeToProcessed++;
            bool pruneNode = false;
            for (const auto& cond : PRUNE_CONDITIONS) {
                ErrorNode& parent = allNodes[parentIdx];
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

            for (RecoveryElement c : generateCases(allNodes[parentIdx].preNextRecoveryState)) {
                parser.restore(allNodes[parentIdx].preNextRecoveryState);

                // if (c.isInsert())
                //     dbgln("Inserting '{}' at \"{}\"", exampleString(c.insert()), allNodes[parentIdx].preNextRecoveryState.sourcePosition);
                // else
                //     dbgln("Skipping 1 at \"{}\"", allNodes[parentIdx].preNextRecoveryState.sourcePosition);
                if (parser.apply(NoOutput(), c) != ReturnStatus::Ready) {
                    // if (c.isInsert())
                    //     dbgln("Inserting '{}' failed: {}", exampleString(c.insert()), formatInternalErrorMessage({ parser.save(), parser.skipToken() }));
                    continue;
                }
                int_t prevParsedTokens = parser.parsedTokens();
                parser.parse(NoOutput());
                if (parser.done()) {
                    insertNode({
                        .parentNode = (uint32_t)parentIdx,
                        .recovery = c,
                        .sourceTokenIndex = (uint32_t)(allNodes[parentIdx].sourceTokenIndex + c.skips() + parser.parsedTokens() - prevParsedTokens),
                        .totalSkippedTokens = (uint32_t)(allNodes[parentIdx].totalSkippedTokens + c.skips()),
                        .totalInsertedTokens = (uint32_t)(allNodes[parentIdx].totalInsertedTokens + c.inserts()),
                        .preNextRecoveryState = parser.save(),
                    });
                    continue;
                }

                VERIFY(parser.error());
                int_t validTokensUntilError = parser.parsedTokens() - prevParsedTokens;
                parser.restore(allNodes[parentIdx].preNextRecoveryState);
                VERIFY(parser.apply(NoOutput(), c) == ReturnStatus::Ready);
                makeNodes(
                    parentIdx, c,
                    allNodes[parentIdx].sourceTokenIndex,
                    allNodes[parentIdx].totalSkippedTokens + c.skips(),
                    allNodes[parentIdx].totalInsertedTokens + c.inserts(),
                    parser, validTokensUntilError);
            }
        }
    }

    std::vector<ReturnElement> recoveryPath(int_t nodeIdx) const {
        std::vector<ReturnElement> result;
        for (;;) {
            const auto& node = allNodes[nodeIdx];
            int_t parentIdx = node.parentNode;
            if (parentIdx == (int_t)(uint32_t)limits::max)
                break;
            const auto& parent = allNodes[parentIdx];
            result.push_back(ReturnElement {
                .totalSkippedTokens = parent.totalSkippedTokens,
                .totalInsertedTokens = parent.totalInsertedTokens,
                .preRecoveryState = parent.preNextRecoveryState,
                .recovery = node.recovery });
            nodeIdx = parentIdx;
        }
        std::ranges::reverse(result);
        return result;
    }

    static std::vector<std::vector<ReturnElement>> recover(SimpleParser& parser, const SavedParserState& errorState) {
        VERIFY(parser.parsedTokens() == 0);
        RecoveryState state;
        state.makeNodes(limits::max, RecoveryElement::insert(LexerToken::Invalid), 0, 0, 0, parser, errorState.parsedTokens);
        state.grind(parser);

        std::vector<std::vector<ReturnElement>> result;
        for (const ErrorInfo& info : state.bestErrors) {
            const auto& node = state.allNodes[info.nodeIdx];
            if (node.preNextRecoveryState.status == ReturnStatus::EOS)
                result.emplace_back(state.recoveryPath(info.nodeIdx));
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

    int_t nextNodeToProcessed = 0;
    StateTable stateTable = {};
    std::vector<ErrorInfo> bestErrors = {};
    std::vector<ErrorNode> allNodes = {};
    std::unique_ptr<ErrorNode> rootError = nullptr;
};

struct RecoveryGroup {
    RecoveryInstructions recovery;
    uint32_t sourceTokenPosition;
    bool unanimousAndIsolated = false;
    SavedParserState preFirstRecoveryState;

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
                .preFirstRecoveryState = std::move(element.preRecoveryState) });
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

        int_t maxPrevPos = limits::min;
        int_t minNextPos = limits::max;
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
        result.push_back(RecoveredError(
            std::move(elem.preFirstRecoveryState),
            std::move(elem.recovery),
            elem.unanimousAndIsolated));
    }
    return result;
}

[[maybe_unused]] static std::string buildMockupString(std::string_view source, std::span<const RecoveryState::ReturnElement> path) {
    SimpleParser parser(source.data());
    std::string result;
    for (auto elem : path) {
        const char* prev = parser.sourcePosition();
        parser.parse(NoOutput(), (int_t)elem.preRecoveryState.parsedTokens - parser.parsedTokens());
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
    std::string_view source = "fn f(): { return a + a b; }";
    SimpleParser parser(source.data());
    parser.parse(NoOutput());
    EXPECT_FALSE(parser.done());
    auto results = RecoveryState::recover(source, parser.save());
    ASSERT_EQ(results.size(), 2);
    for (auto& result : results) {
        ASSERT_EQ(result.size(), 1);
        EXPECT_EQ(result.front().recovery, RecoveryElement::skip1());
    }
    auto mockup1 = buildMockupString(source, results[0]);
    auto mockup2 = buildMockupString(source, results[1]);
    EXPECT_EQ(mockup1, "fn f(): { return a + a; }");
    EXPECT_EQ(mockup2, "fn f(): { return a + b; }");
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
    EXPECT_EQ(result.front().recovery, RecoveryElement::skip1());
}

TEST(Parse, RecoveryBasic12) {
    std::string_view source = "fn f1(): { return a + a a; } fn f2(): { return (a + ); }";
    SimpleParser parser(source.data());
    parser.parse(NoOutput());
    EXPECT_FALSE(parser.done());
    auto results = RecoveryState::recover(source, parser.save());
    ASSERT_EQ(results.size(), 2);
    for (auto& result : results) {
        ASSERT_EQ(result.size(), 2);
        EXPECT_EQ(result[0].recovery, RecoveryElement::skip1());
        EXPECT_EQ(result[1].recovery, RecoveryElement::skip1());
    }
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
    EXPECT_EQ(result[0].recovery, RecoveryElement::skip1());
    EXPECT_EQ(result[1].recovery, RecoveryElement::insert(LexerToken::RightParen));
    EXPECT_EQ(result[2].recovery, RecoveryElement::insert(LexerToken::SemiColon));
    EXPECT_EQ(result[3].recovery, RecoveryElement::insert(LexerToken::RightBrace));
    auto mockup = buildMockupString(source, result);
    EXPECT_EQ(mockup, "fn f(): { (a) ; }  ");
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

TEST(Parse, DISABLED_RecoveryMockup) {
    std::string_view source = "struct A: { base ; }";
    SimpleParser parser(source.data());
    parser.parse(NoOutput());
    EXPECT_FALSE(parser.done());
    auto results = RecoveryState::recover(source, parser.save());
    for (auto& result : results) {
        auto mockup = buildMockupString(source, result);
        EXPECT_EQ(mockup, "");
    }
}

}