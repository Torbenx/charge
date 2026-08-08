#include <verify/backend/KeyWatches.h>

#include <verify/backend/KeyWatches.impl.h>
#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

namespace {

    struct ReportedMatch {
        Value key;
        Value watch;

        bool operator==(const ReportedMatch&) const = default;
    };

    using Reported = std::vector<ReportedMatch>;

    //! Watches of memory declarations, matching them by the equality of the solver
    struct TestWatches : KeyWatches<TestWatches> {
        static constexpr KeyWatchesParams PARAMS = {
            .keyUse = UseKind::KeyWatchesTestKey,
            .watchUse = UseKind::KeyWatchesTestWatch,
        };

        void onKeyMatch(Solver&, ElementId, Value key, Value watch) {
            reported.push_back({ key, watch });
        }

        std::vector<ReportedMatch> reported;
    };

    //! A set of memory declarations to watch, and the structure watching them
    struct WatchFixture {
        SolverImpl solver;
        TestWatches watches;

        Sets::ElementId e1 { 1 };
        Sets::ElementId e2 { 2 };

        MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
        MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
        MemoryDeclaration d3 = solver.newAuxMemoryDeclarationVariable();

        void setKey(Sets::ElementId element, Value key) {
            watches.setKey(solver, element, key);
            watches.checkInvariances(solver);
        }

        void addWatch(Sets::ElementId element, Value watch) {
            watches.addWatch(solver, element, watch);
            watches.checkInvariances(solver);
        }

        //! The matches reported since the last call
        std::vector<ReportedMatch> takeReported() {
            return std::exchange(watches.reported, {});
        }

        //! Hand the notifications the solver made to the watches, as the owning theory would
        void propagateRewrites() {
            for (Use use : std::exchange(solver.useTest.rewrites, {}))
                watches.propagateRewrite(solver, use);
            watches.checkInvariances(solver);
        }

        void decideEqual(Value a, Value b) {
            solver.decideTrue(solver.equality(a, b));
            solver.propagate();
            watches.newDecisionLevel(solver);
            propagateRewrites();
        }

        //! Start a decision level without touching any of the declarations
        void newDecisionLevel() {
            solver.decideTrue(solver.newAuxBooleanVariable());
            solver.propagate();
            watches.newDecisionLevel(solver);
            propagateRewrites();
        }

        void backtrack(int_t level) {
            solver.backtrack(level);
            solver.propagate();
            watches.beginBacktrack(solver);
            watches.checkInvariances(solver);
        }
    };

}

TEST(VerifyBackend, KeyWatchesMatchOfAWatchAddedBeforeTheKey) {
    WatchFixture f;
    f.addWatch(f.e1, f.d2);
    EXPECT_TRUE(f.takeReported().empty());

    // The watch does not match the key yet, the two declarations are unrelated so far
    f.setKey(f.e1, f.d1);
    EXPECT_TRUE(f.takeReported().empty());
    EXPECT_TRUE(f.watches.keyOf(f.e1) == std::optional<Value>(f.d1));

    f.decideEqual(f.d1, f.d2);
    EXPECT_TRUE((f.takeReported() == Reported { { f.d1, f.d2 } }));
}

TEST(VerifyBackend, KeyWatchesMatchOfAWatchAddedAfterTheKey) {
    WatchFixture f;
    f.setKey(f.e1, f.d1);
    f.decideEqual(f.d1, f.d2);
    EXPECT_TRUE(f.takeReported().empty());

    // Both of these match the moment they are added, the key itself trivially so
    f.addWatch(f.e1, f.d2);
    EXPECT_TRUE((f.takeReported() == Reported { { f.d1, f.d2 } }));

    f.addWatch(f.e1, f.d1);
    EXPECT_TRUE((f.takeReported() == Reported { { f.d1, f.d1 } }));
}

TEST(VerifyBackend, KeyWatchesReportEveryMatchOnce) {
    WatchFixture f;
    f.setKey(f.e1, f.d1);
    f.addWatch(f.e1, f.d2);
    f.addWatch(f.e1, f.d2);

    // The two watches match separately, even though they watch the same value
    f.decideEqual(f.d1, f.d2);
    EXPECT_TRUE((f.takeReported() == Reported { { f.d1, f.d2 }, { f.d1, f.d2 } }));

    // Later rewrites of the values of a match do not report it again
    f.decideEqual(f.d2, f.d3);
    EXPECT_TRUE(f.takeReported().empty());
}

TEST(VerifyBackend, KeyWatchesAreSeparateForEachElement) {
    WatchFixture f;
    f.setKey(f.e1, f.d1);
    f.setKey(f.e2, f.d3);
    f.addWatch(f.e2, f.d2);

    // The watch belongs to the element of the other key, so this is no match
    f.decideEqual(f.d1, f.d2);
    EXPECT_TRUE(f.takeReported().empty());

    f.decideEqual(f.d2, f.d3);
    EXPECT_TRUE((f.takeReported() == Reported { { f.d3, f.d2 } }));
}

TEST(VerifyBackend, KeyWatchesBacktrackAMatch) {
    WatchFixture f;
    f.newDecisionLevel();
    f.setKey(f.e1, f.d1);
    f.addWatch(f.e1, f.d2);
    f.decideEqual(f.d1, f.d2);
    EXPECT_EQ(f.takeReported().size(), 1u);

    // The key and the watch of the level below survive, only the match is reverted with the equality
    f.backtrack(1);
    EXPECT_TRUE(f.watches.keyOf(f.e1) == std::optional<Value>(f.d1));

    // So the same match is reported again once it is established anew
    f.decideEqual(f.d2, f.d1);
    EXPECT_TRUE((f.takeReported() == Reported { { f.d1, f.d2 } }));
}

TEST(VerifyBackend, KeyWatchesBacktrackKeysAndWatches) {
    WatchFixture f;
    f.newDecisionLevel();
    f.addWatch(f.e1, f.d2);

    f.newDecisionLevel();
    f.setKey(f.e1, f.d1);
    f.addWatch(f.e1, f.d3);
    f.decideEqual(f.d1, f.d3);
    EXPECT_EQ(f.takeReported().size(), 1u);

    // The key and the watch of the reverted levels are gone, the earlier watch remains
    f.backtrack(1);
    EXPECT_FALSE(f.watches.keyOf(f.e1).has_value());

    f.setKey(f.e1, f.d3);
    EXPECT_TRUE(f.takeReported().empty());
    f.decideEqual(f.d2, f.d3);
    EXPECT_TRUE((f.takeReported() == Reported { { f.d3, f.d2 } }));
}

namespace {

    //! Watches without a payload, testing the default of KeyWatches
    struct PlainWatches : KeyWatches<PlainWatches> {
        static constexpr KeyWatchesParams PARAMS = {
            .keyUse = UseKind::KeyWatchesTestKey,
            .watchUse = UseKind::KeyWatchesTestWatch,
        };

        void onKeyMatch(Solver&, ElementId, Value, Value) { matchCount++; }

        int_t matchCount = 0;
    };

}

TEST(VerifyBackend, KeyWatchesWithoutWatchData) {
    SolverImpl solver;
    PlainWatches watches;
    Sets::ElementId element { 1 };
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();

    watches.setKey(solver, element, d1);
    watches.addWatch(solver, element, d2);
    EXPECT_EQ(watches.matchCount, 0);

    solver.decideTrue(solver.equality(d1, d2));
    solver.propagate();
    watches.newDecisionLevel(solver);
    for (Use use : std::exchange(solver.useTest.rewrites, {}))
        watches.propagateRewrite(solver, use);

    EXPECT_EQ(watches.matchCount, 1);
    watches.checkInvariances(solver);
}

}
