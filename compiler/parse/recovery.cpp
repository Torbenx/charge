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
    uint32_t cost() const { return isInsert() ? 3 : 2; }

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

}

namespace parse::recovery {

struct StateHandle {
    constexpr StateHandle(uint32_t id)
        : m_id(id) { }
    constexpr uint32_t id() const { return m_id; }
    bool operator==(const StateHandle&) const = default;
    uint32_t m_id;
};

struct EdgeHandle {
    constexpr EdgeHandle(uint32_t id)
        : m_id(id) { }
    constexpr uint32_t id() const { return m_id; }
    bool operator==(const EdgeHandle&) const = default;
    uint32_t m_id;
};

}

template<>
struct optional_traits<parse::recovery::StateHandle> {
    static constexpr auto empty_value = parse::recovery::StateHandle(limits::max);
};

template<>
struct optional_traits<parse::recovery::EdgeHandle> {
    static constexpr auto empty_value = parse::recovery::EdgeHandle(limits::max);
};

namespace parse::recovery {

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
        //! Cost of the cheapest known way to the state, final once the state is expanded
        uint32_t modificationCost = limits::max;
        //! All the ways to the state that cost modificationCost
        std::optional<EdgeHandle> lastIncomingEdge;
        uint32_t incomingEdges = 0;
        bool expanded = false;
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

    //! The parsed token count is not stored with the state and has to be supplied by the caller
    SavedParserState savedState(StateHandle handle, uint32_t parsedTokens) const {
        const StateData& data = m_states[handle.id()];
        return { data.key.status, data.key.state, parsedTokens, data.key.sourcePosition, data.key.scopes };
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

    //! Pruning condition used once the search exceeds MAX_STATES
    /*!
    It only keeps the states that are furthest ahead, so the search still reaches the end of the
    source but reports worse recoveries for the rest of it.
    */
    static constexpr PruneCondition STRICT_PRUNE_CONDITION { .tokenLookAhead = 1, .costTolerance = 0 };
    static constexpr int_t MAX_STATES = 100000;

    //! Maximum number of equally good ways to a state that are remembered
    static constexpr uint32_t MAX_INCOMING_EDGES = 4;
    //! Maximum number of alternative recovery paths that are reported
    static constexpr int_t MAX_RECOVERY_PATHS = 8;

    using StateData = StateTable::StateData;

    //! Applying a recovery element to its parent state leads to the state holding the edge
    struct Edge {
        StateHandle parent;
        RecoveryElement recovery;
        //! Implements singly linked list of incoming edges to the same target
        std::optional<EdgeHandle> prevIncomingEdge;
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
        ErrorInfo(StateHandle state, const StateData& data)
            : state(state)
            , totalAdvancedTokens(data.sourceTokenIndex)
            , modificationCost(data.modificationCost) { }

        StateHandle state;
        uint32_t totalAdvancedTokens = 0;
        uint32_t modificationCost = 0;

        bool operator==(const ErrorInfo&) const = default;
    };

    struct CompareByTotalAdvance {
        bool operator()(const ErrorInfo& l, const ErrorInfo& r) const {
            return l.totalAdvancedTokens > r.totalAdvancedTokens;
        }
    };

    void insertBestError(ErrorInfo info) {
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

    bool isPruned(const StateData& state) const {
        std::span<const PruneCondition> conditions = PRUNE_CONDITIONS;
        if (stateTable.size() > MAX_STATES)
            conditions = { &STRICT_PRUNE_CONDITION, 1 };

        for (const auto& cond : conditions) {
            auto it = std::partition_point(bestErrors.begin(), bestErrors.end(), [&](const ErrorInfo& info) {
                return info.totalAdvancedTokens >= state.sourceTokenIndex + cond.tokenLookAhead;
            });
            if (it == bestErrors.begin())
                continue;
            VERIFY(std::prev(it)->totalAdvancedTokens >= state.sourceTokenIndex + cond.tokenLookAhead);
            if (std::prev(it)->modificationCost + cond.costTolerance <= state.modificationCost)
                return true;
        }
        return false;
    }

    void enqueue(StateHandle handle, uint32_t modificationCost) {
        if (modificationCost >= queue.size())
            queue.resize(modificationCost + 1);
        queue[modificationCost].push_back(handle);
    }

    void addRootState(const SavedParserState& savedState, uint32_t sourceTokenIndex) {
        auto [handle, isNew] = stateTable.intern(savedState, sourceTokenIndex);
        if (!isNew)
            return;
        stateTable[handle].modificationCost = 0;
        enqueue(handle, 0);
    }

    void addState(StateHandle parent, RecoveryElement recovery, uint32_t sourceTokenIndex, const SavedParserState& savedState) {
        uint32_t modificationCost = stateTable[parent].modificationCost + recovery.cost();
        auto [handle, isNew] = stateTable.intern(savedState, sourceTokenIndex);
        StateData& state = stateTable[handle];
        if (isNew || modificationCost < state.modificationCost) {
            // Since every edge increases the cost and states are expanded in the order of their
            // cost, a cheaper way to a state can only be found before it is expanded
            VERIFY(!state.expanded);
            state.modificationCost = modificationCost;
            state.lastIncomingEdge = std::nullopt;
            state.incomingEdges = 0;
            enqueue(handle, modificationCost);
        } else if (modificationCost > state.modificationCost || state.incomingEdges >= MAX_INCOMING_EDGES) {
            return;
        }

        edges.push_back({ .parent = parent, .recovery = recovery, .prevIncomingEdge = state.lastIncomingEdge });
        state.lastIncomingEdge = EdgeHandle { (uint32_t)edges.size() - 1 };
        state.incomingEdges += 1;
    }

    void expandState(SimpleParser& parser, StateHandle handle) {
        uint32_t sourceTokenIndex = stateTable[handle].sourceTokenIndex;
        SavedParserState savedState = stateTable.savedState(handle, sourceTokenIndex);
        for (RecoveryElement c : generateCases(savedState)) {
            parser.restore(savedState);

            // if (c.isInsert())
            //     dbgln("Inserting '{}' at \"{}\"", exampleString(c.insert()), savedState.sourcePosition);
            // else
            //     dbgln("Skipping 1 at \"{}\"", savedState.sourcePosition);
            if (parser.apply(NoOutput(), c) != ReturnStatus::Ready) {
                // if (c.isInsert())
                //     dbgln("Inserting '{}' failed: {}", exampleString(c.insert()), formatInternalErrorMessage({ parser.save(), parser.skipToken() }));
                continue;
            }
            int_t prevParsedTokens = parser.parsedTokens();
            parser.parse(NoOutput());
            uint32_t index = sourceTokenIndex + (uint32_t)c.skips();
            if (parser.done()) {
                addState(handle, c, index + (uint32_t)(parser.parsedTokens() - prevParsedTokens), parser.save());
                continue;
            }

            VERIFY(parser.error());
            int_t validTokensUntilError = parser.parsedTokens() - prevParsedTokens;
            parser.restore(savedState);
            VERIFY(parser.apply(NoOutput(), c) == ReturnStatus::Ready);
            if (validTokensUntilError > 1) {
                parser.parse(NoOutput(), validTokensUntilError - 1);
                addState(handle, c, index + (uint32_t)validTokensUntilError - 1, parser.save());
                parser.parse(NoOutput(), 1);
            } else {
                parser.parse(NoOutput(), validTokensUntilError);
            }
            addState(handle, c, index + (uint32_t)validTokensUntilError, parser.save());
        }
    }

    void grind(SimpleParser& parser) {
        for (uint32_t cost = 0; cost < queue.size(); cost++) {
            // Nothing is added to the current bucket while it is drained since every edge increases
            // the cost. Expanding the states that are furthest ahead first lets the pruning
            // conditions take effect earlier.
            std::ranges::sort(queue[cost], std::less(), [this](StateHandle handle) {
                return stateTable[handle].sourceTokenIndex;
            });
            while (!queue[cost].empty()) {
                StateHandle handle = queue[cost].back();
                queue[cost].pop_back();
                StateData& state = stateTable[handle];
                if (state.expanded || state.modificationCost != cost)
                    continue;
                state.expanded = true;

                insertBestError({ handle, state });
                if (isPruned(state))
                    continue;
                expandState(parser, handle);
            }
        }
    }

    //! Collects up to MAX_RECOVERY_PATHS of the ways to reach the state as edge sequences
    void collectPaths(StateHandle handle, std::vector<EdgeHandle>& path, std::vector<std::vector<EdgeHandle>>& result) const {
        std::vector<EdgeHandle> incoming;
        for (std::optional<EdgeHandle> edge = stateTable[handle].lastIncomingEdge; edge.has_value(); edge = edges[edge->id()].prevIncomingEdge) {
            VERIFY(incoming.size() < MAX_INCOMING_EDGES);
            incoming.push_back(edge.value());
        }
        if (incoming.empty()) {
            result.emplace_back(path.rbegin(), path.rend());
            return;
        }

        // The incoming edges are stored in the reverse order of their discovery
        for (EdgeHandle edge : std::views::reverse(incoming)) {
            if ((int_t)result.size() >= MAX_RECOVERY_PATHS)
                return;
            path.push_back(edge);
            collectPaths(edges[edge.id()].parent, path, result);
            path.pop_back();
        }
    }

    //! Replays the path to recompute the parser states
    /*!
    This is required to recover the number tokens parsed along the path.
    */
    std::vector<ReturnElement> buildPath(std::string_view source, std::span<const EdgeHandle> path) const {
        SimpleParser parser(source.data());
        std::vector<ReturnElement> result;
        uint32_t totalSkippedTokens = 0;
        uint32_t totalInsertedTokens = 0;
        for (EdgeHandle edge : path) {
            const Edge& incoming = edges[edge.id()];
            const StateData& state = stateTable[incoming.parent];
            int_t advancedTokens = parser.parsedTokens() + (int_t)totalSkippedTokens - (int_t)totalInsertedTokens;
            VERIFY(parser.parse(NoOutput(), (int_t)state.sourceTokenIndex - advancedTokens) == ReturnStatus::Ready);
            SavedParserState preRecoveryState = parser.save();
            VERIFY(stateTable.savedState(incoming.parent, preRecoveryState.parsedTokens) == preRecoveryState);

            result.push_back(ReturnElement {
                .totalSkippedTokens = totalSkippedTokens,
                .totalInsertedTokens = totalInsertedTokens,
                .preRecoveryState = std::move(preRecoveryState),
                .recovery = incoming.recovery });
            VERIFY(parser.apply(NoOutput(), incoming.recovery) == ReturnStatus::Ready);
            totalSkippedTokens += (uint32_t)incoming.recovery.skips();
            totalInsertedTokens += (uint32_t)incoming.recovery.inserts();
        }
        return result;
    }

    static std::vector<std::vector<ReturnElement>> recover(std::string_view source, const SavedParserState& errorState) {
        RecoveryState state;
        SimpleParser parser(source.data());
        // The states the parser passes through before the first error are the roots of the search
        int_t validTokensUntilError = errorState.parsedTokens;
        if (validTokensUntilError > 1) {
            parser.parse(NoOutput(), validTokensUntilError - 1);
            state.addRootState(parser.save(), (uint32_t)validTokensUntilError - 1);
            parser.parse(NoOutput(), 1);
        } else {
            parser.parse(NoOutput(), validTokensUntilError);
        }
        state.addRootState(parser.save(), (uint32_t)validTokensUntilError);
        state.grind(parser);

        std::vector<std::vector<EdgeHandle>> paths;
        std::vector<EdgeHandle> path;
        for (const ErrorInfo& info : state.bestErrors) {
            if (state.stateTable[info.state].key.status == ReturnStatus::EOS)
                state.collectPaths(info.state, path, paths);
        }

        std::vector<std::vector<ReturnElement>> result;
        for (const auto& edgePath : paths)
            result.emplace_back(state.buildPath(source, edgePath));
        return result;
    }

    void checkInvariances() {
        for (int_t i = 1; i < (int_t)bestErrors.size(); i++) {
            VERIFY(bestErrors[i - 1].totalAdvancedTokens >= bestErrors[i].totalAdvancedTokens);
            VERIFY(bestErrors[i - 1].modificationCost >= bestErrors[i].modificationCost);
            if (bestErrors[i - 1].totalAdvancedTokens == bestErrors[i].totalAdvancedTokens)
                VERIFY(bestErrors[i - 1].modificationCost == bestErrors[i].modificationCost);
        }
    }

    StateTable stateTable = {};
    std::vector<Edge> edges = {};
    //! States that still have to be expanded, indexed by their modification cost
    std::vector<std::vector<StateHandle>> queue = {};
    std::vector<ErrorInfo> bestErrors = {};
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
    auto ungroupedPaths = RecoveryState::recover(source, rootErrorState);
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