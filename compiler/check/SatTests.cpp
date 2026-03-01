#include <gtest/gtest.h>

#include <check/BooleanVariables.h>
#include <check/EqualityTheory.h>
#include <check/Phis.h>
#include <check/SatSolver.h>
#include <check/SimpleVariables.h>
#include <check/StandardEquality.h>
#include <check/StandardLoads.h>
#include <check/StoreBlocks.h>
#include <check/TopologicalOrder.h>
#include <check/Types.h>

#include <filesystem>
#include <fstream>

namespace check::sat {

namespace {
    struct Parser {
        using Clause = std::vector<int_t>;

        std::vector<Clause> cnf;
        int_t clauseCount = 0;
        int_t variableCount = 0;

        std::string_view buffer;

        void skipToNextLine() {
            while (!buffer.empty() && buffer.front() != '\r' && buffer.front() != '\n')
                buffer = buffer.substr(1);
            if (buffer.empty())
                return;
            if (buffer.front() == '\r' && buffer.size() > 1 && buffer[1] == '\n')
                buffer = buffer.substr(2);
            else
                buffer = buffer.substr(1);
        }

        void skipSpaces() {
            while (!buffer.empty() && buffer.front() == ' ')
                buffer = buffer.substr(1);
        }

        void skipWhitespace() {
            while (!buffer.empty() && (buffer.front() == ' ' || buffer.front() == '\t' || buffer.front() == '\n' || buffer.front() == '\r'))
                buffer = buffer.substr(1);
        }

        int_t parseInteger() {
            skipSpaces();
            VERIFY(!buffer.empty());
            bool negative = false;
            if (buffer.front() == '-') {
                negative = true;
                buffer = buffer.substr(1);
            }
            int_t value = 0;
            while (buffer.length() > 0) {
                char c = buffer.front();
                if (c < '0' || c > '9')
                    break;
                buffer = buffer.substr(1);

                value = value * 10 + (c - '0');
            }
            return negative ? -value : value;
        }

        void parse() {
            while (!buffer.empty() && buffer.front() == 'c')
                skipToNextLine();
            VERIFY(!buffer.empty());

            VERIFY(buffer.front() == 'p');
            buffer = buffer.substr(1);
            skipSpaces();

            VERIFY(buffer.starts_with("cnf"));
            buffer = buffer.substr(3);
            skipSpaces();

            variableCount = parseInteger();
            clauseCount = parseInteger();
            skipToNextLine();

            for (int_t i = 0; i < clauseCount; i++) {
                Clause clause;
                for (;;) {
                    skipWhitespace();
                    int_t v = parseInteger();
                    if (v == 0)
                        break;
                    clause.push_back(v);
                }
                cnf.emplace_back(std::move(clause));
            }

            VERIFY((int_t)cnf.size() == clauseCount);
        }
    };

    std::optional<std::vector<bool>> check(const Parser& parser) {
        // setup
        Solver solver;
        BooleanVariables theory(solver);

        // generate
        VERIFY(theory.newVariable(solver) == 0);
        for (int_t varId = 1; varId <= parser.variableCount; varId++)
            VERIFY(theory.newVariable(solver) == varId);
        for (const auto& clause : parser.cnf) {
            std::vector<BooleanValue> outClause;
            for (int_t i = 0; i < (int_t)clause.size(); i++)
                outClause.push_back(theory.literalFromSign(clause[i]));
            solver.addClause(std::move(outClause));
        }

        if (solver.hasConflicts() || !solver.propagate())
            return std::nullopt; // unsat

        // solver
        for (;;) {
            auto var = theory.findUnassignedVariable(solver);
            if (!var.has_value())
                break;

            int_t varId = var.value();
            solver.decideTrue(theory.negativeLiteral(varId));
            VERIFY(!solver.hasConflicts());
            while (!solver.propagate()) {
                if (!solver.analyzeConflicts())
                    return std::nullopt; // unsat
            }
        }

        // sat
        VERIFY(solver.checkAssignment());
        std::vector<bool> assignment;
        for (int_t varId = 1; varId <= parser.variableCount; varId++) {
            if (theory.assignedPositive(solver, varId))
                assignment.push_back(true);
            else if (theory.assignedNegative(solver, varId))
                assignment.push_back(false);
            else
                VERIFY_NOT_REACHED();
        }
        return assignment;
    }

    std::string readFile(std::filesystem::path file) {
        std::ifstream stream;
        stream.open(file, std::ios::binary);
        VERIFY(stream.good());
        stream.seekg(0, std::ios::end);
        int_t length = stream.tellg();
        VERIFY(length >= 0);
        std::string sourceBuffer;
        sourceBuffer.resize(length);
        stream.seekg(0, std::ios::beg);
        stream.read(sourceBuffer.data(), length);
        stream.close();
        VERIFY(stream.good());

        return sourceBuffer;
    }

    void writeFile(std::filesystem::path file, std::string content) {
        std::ofstream stream;
        stream.open(file, std::ios::binary);
        VERIFY(stream.good());
        stream.write(content.data(), content.length());
        stream.close();
        VERIFY(stream.good());
    }
}

TEST(Check, SatProblems) {
    std::filesystem::path testDir = COMPILER_TEST_DIR "/sat";
    bool overwriteSolutionFiles = false;

    namespace fs = std::filesystem;
    int_t count = 0;
    for (const auto& entry : fs::directory_iterator(testDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cnf")
            continue;

        auto sourceBuffer = readFile(entry.path());
        Parser parser;
        parser.buffer = sourceBuffer;
        parser.parse();
        auto result = check(parser);
        std::string resultString;
        if (result.has_value()) {
            for (bool v : result.value())
                resultString += v ? "true " : "false ";
        }

        auto solutionPath = entry.path();
        solutionPath += ".sol";
        if (overwriteSolutionFiles)
            writeFile(solutionPath, resultString);

        auto expectedResultString = readFile(solutionPath);
        EXPECT_TRUE(resultString == expectedResultString);

        count += 1;
    }
    EXPECT_EQ(count, 80);
}

using TestEquality = BasicEquality;

static constexpr ValueKind TEST_VALUE_KIND = (ValueKind)-1;

struct TestKindTheory : ValueKindTheory {
    TestKindTheory(Solver& solver)
        : ValueKindTheory(solver, TEST_VALUE_KIND), m_equality(solver, TEST_VALUE_KIND) { }

    std::string formatValueKind(Solver&, ValueKind) override {
        return "test-value";
    }

    BooleanValue equality(Solver& solver, Value a, Value b) override {
        return m_equality.equality(solver, a, b);
    }

    Value defineLoad(Solver&, MemoryLocation, CodePosition) override {
        VERIFY_NOT_REACHED();
    }

    bool testReason(Solver& solver, BooleanValue eq) {
        return m_equality.testReason(solver, eq, m_equality.equalityReason());
    }

    auto getEqualityClause(Solver& solver, BooleanValue eq) {
        return m_equality.reasonToClause(solver, eq, m_equality.equalityReason());
    }

    TestEquality m_equality;
};

struct TestValueTheory : ValueTheory {
    TestValueTheory(Solver& solver)
        : ValueTheory(solver, TEST_VALUE_KIND), baseLabel(solver, ValueCategory::Load) { }

    uint64_t labelOfValue(Solver&, Value v) override { return baseLabel + v.valueId; }
    std::string formatValue(Solver&, Value v) override { return fmt::format("v{}", v.valueId + 1); }

    ValueBaseLabel baseLabel;
};

TEST(Check, EqualityTreePath1) {
    Solver solver;
    solver.propagate();
    TestKindTheory kind(solver);
    TestValueTheory values(solver);
    Value v1 = values.newValue(solver);
    Value v2 = values.newValue(solver);

    BooleanValue e12 = solver.equality(v1, v2);
    EXPECT_FALSE(kind.testReason(solver, e12));

    solver.decideTrue(e12);
    EXPECT_FALSE(kind.testReason(solver, e12));

    solver.propagate();
    EXPECT_TRUE(kind.testReason(solver, e12));

    {
        auto [clause, forcedIndex] = kind.getEqualityClause(solver, e12);
        EXPECT_EQ(clause.size(), 2);
        EXPECT_EQ(forcedIndex, 0);
        EXPECT_EQ(clause[0], e12);
        EXPECT_EQ(clause[1], !e12);
    }

    solver.backtrack(0);
    {
        auto [clause, forcedIndex] = kind.getEqualityClause(solver, e12);
        EXPECT_EQ(clause.size(), 2);
        EXPECT_EQ(forcedIndex, 0);
        EXPECT_EQ(clause[0], e12);
        EXPECT_EQ(clause[1], !e12);
    }
}

TEST(Check, EqualityTreePath2) {
    Solver solver;
    solver.propagate();
    TestKindTheory kind(solver);
    TestValueTheory values(solver);
    Value v1 = values.newValue(solver);
    Value v2 = values.newValue(solver);
    Value v3 = values.newValue(solver);

    BooleanValue e12 = solver.equality(v1, v2);
    BooleanValue e13 = solver.equality(v1, v3);
    BooleanValue e23 = solver.equality(v2, v3);
    solver.decideTrue(e12);
    solver.propagate();
    solver.decideTrue(e13);
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(e23));
    EXPECT_TRUE(kind.testReason(solver, e23));

    auto [clause, forcedIndex] = kind.getEqualityClause(solver, e23);
    EXPECT_EQ(clause.size(), 3);
    EXPECT_EQ(clause[forcedIndex], e23);
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), !e12) != clause.end());
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), !e13) != clause.end());
}

TEST(Check, EqualityTreePath3) {
    Solver solver;
    solver.propagate();
    TestKindTheory kind(solver);
    TestValueTheory values(solver);
    Value v1 = values.newValue(solver);
    Value v2 = values.newValue(solver);
    Value v3 = values.newValue(solver);

    BooleanValue e13 = solver.equality(v1, v3);
    BooleanValue e23 = solver.equality(v2, v3);
    BooleanValue e12 = solver.equality(v1, v2);
    solver.decideTrue(e13);
    solver.propagate();
    solver.decideTrue(e23);
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(e12));
    EXPECT_TRUE(kind.testReason(solver, e12));

    auto [clause, forcedIndex] = kind.getEqualityClause(solver, e12);
    EXPECT_EQ(clause.size(), 3);
    EXPECT_EQ(clause[forcedIndex], e12);
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), !e13) != clause.end());
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), !e23) != clause.end());
}

TEST(Check, EqualityTreePath4) {
    Solver solver;
    solver.propagate();
    TestKindTheory kind(solver);
    TestValueTheory values(solver);
    Value vals[4][4];
    for (int_t i = 0; i < 4; i++) {
        for (int_t j = 0; j < 4; j++)
            vals[i][j] = values.newValue(solver);
    }

    solver.decideTrue(solver.equality(vals[0][0], vals[0][1]));
    solver.propagate();
    solver.decideTrue(solver.equality(vals[0][1], vals[0][2]));
    solver.propagate();
    solver.decideTrue(solver.equality(vals[0][2], vals[0][3]));
    solver.propagate();
    BooleanValue e00_03 = solver.equality(vals[0][0], vals[0][3]);
    EXPECT_TRUE(kind.testReason(solver, e00_03));
    {
        auto [clause, forcedIndex] = kind.getEqualityClause(solver, e00_03);
        EXPECT_EQ(clause.size(), 4);
        EXPECT_EQ(clause[forcedIndex], solver.equality(vals[0][0], vals[0][3]));
        EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][0], vals[0][1])) != clause.end());
        EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][1], vals[0][2])) != clause.end());
        EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][2], vals[0][3])) != clause.end());
    }
    solver.propagate();

    for (int_t i = 0; i < 4; i++) {
        solver.decideTrue(solver.equality(vals[2][i], vals[3][i]));
        solver.propagate();
        solver.decideTrue(solver.equality(vals[1][i], vals[2][i]));
        solver.propagate();
        solver.decideTrue(solver.equality(vals[0][i], vals[1][i]));
        solver.propagate();
    }

    BooleanValue e30_33 = solver.equality(vals[3][0], vals[3][3]);
    EXPECT_TRUE(kind.testReason(solver, e30_33));
    BooleanValue e32_33 = solver.equality(vals[3][2], vals[3][3]);
    EXPECT_TRUE(kind.testReason(solver, e32_33));

    auto testConnections = [&] {
        {
            auto [clause, forcedIndex] = kind.getEqualityClause(solver, e30_33);
            EXPECT_EQ(clause.size(), 10);
            EXPECT_EQ(clause[forcedIndex], solver.equality(vals[3][0], vals[3][3]));

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][0], vals[1][0])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[1][0], vals[2][0])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[2][0], vals[3][0])) != clause.end());

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][0], vals[0][1])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][1], vals[0][2])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][2], vals[0][3])) != clause.end());

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][3], vals[1][3])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[1][3], vals[2][3])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[2][3], vals[3][3])) != clause.end());
        }

        {
            auto [clause, forcedIndex] = kind.getEqualityClause(solver, e32_33);
            EXPECT_EQ(clause.size(), 8);
            EXPECT_EQ(clause[forcedIndex], solver.equality(vals[3][2], vals[3][3]));

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][2], vals[1][2])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[1][2], vals[2][2])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[2][2], vals[3][2])) != clause.end());

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][2], vals[0][3])) != clause.end());

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][3], vals[1][3])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[1][3], vals[2][3])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[2][3], vals[3][3])) != clause.end());
        }
    };
    testConnections();

    solver.backtrack(0);
    EXPECT_FALSE(kind.testReason(solver, e30_33));
    EXPECT_FALSE(kind.testReason(solver, e32_33));

    testConnections();
}

TEST(Check, EqualityPropagation1) {
    Solver solver;
    TestKindTheory kind(solver);
    TestValueTheory values(solver);
    Value v1 = values.newValue(solver);
    Value v2 = values.newValue(solver);
    Value v3 = values.newValue(solver);
    solver.addClause({ solver.equality(v1, v2) });
    solver.addClause({ solver.equality(v2, v3) });
    solver.addClause({ !solver.equality(v1, v3) });
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
}

TEST(Check, EqualityPropagation2) {
    Solver solver;
    TestKindTheory kind(solver);
    TestValueTheory values(solver);
    BooleanVariables bools(solver);
    BooleanValue c = bools.positiveLiteral(bools.newVariable(solver));
    Value s = values.newValue(solver);
    Value t1 = values.newValue(solver);
    Value t2 = values.newValue(solver);
    solver.addClause({ c, solver.equality(s, t1), solver.equality(s, t2) });
    solver.addClause({ solver.negate(c) });
    solver.addClause({ solver.equality(t1, t2) });
    solver.addClause({ !solver.equality(s, t1) });
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
}

TEST(Check, DisequalityPropagation1) {
    Solver solver;
    TestKindTheory kind(solver);
    TestValueTheory values(solver);
    BooleanVariables bools(solver);
    BooleanValue c = bools.positiveLiteral(bools.newVariable(solver));
    Value s = values.newValue(solver);
    Value t1 = values.newValue(solver);
    Value t2 = values.newValue(solver);
    solver.addClause({ c, solver.equality(s, t1), solver.equality(s, t2) });
    solver.addClause({ !solver.equality(s, t1) });
    solver.addClause({ solver.equality(t1, t2) });
    solver.propagate();
    EXPECT_TRUE(solver.assignedTrue(c));
}

TEST(Check, DisequalityPropagation2) {
    Solver solver;
    TestKindTheory kind(solver);
    TestValueTheory values(solver);
    BooleanVariables bools(solver);
    BooleanValue c = bools.positiveLiteral(bools.newVariable(solver));
    Value s = values.newValue(solver);
    Value t1 = values.newValue(solver);
    Value t2 = values.newValue(solver);
    solver.addClause({ c, solver.equality(s, t1), solver.equality(s, t2) });
    solver.addClause({ solver.equality(t1, t2) });
    solver.addClause({ !solver.equality(s, t1) });
    solver.propagate();
    EXPECT_TRUE(solver.assignedTrue(c));
}

TEST(Check, EqualityProblem) {
    Solver solver;
    TestKindTheory kind(solver);
    TestValueTheory values(solver);
    Value s = values.newValue(solver);
    Value t1 = values.newValue(solver);
    Value t2 = values.newValue(solver);
    Value t3 = values.newValue(solver);

    solver.addClause({ !solver.equality(s, t1), !solver.equality(s, t2), solver.equality(s, t3) });
    solver.addClause({ !solver.equality(s, t1), solver.equality(s, t2), !solver.equality(s, t3) });
    solver.addClause({ solver.equality(s, t1), !solver.equality(s, t2), !solver.equality(s, t3) });

    solver.addClause({ !solver.equality(t1, t2), !solver.equality(t1, t3) });
    solver.addClause({ solver.equality(t1, t2), solver.equality(t1, t3), solver.equality(t2, t3) });

    solver.addClause({ !solver.equality(t1, t2), solver.equality(s, t1), solver.equality(s, t2) });
    solver.addClause({ !solver.equality(t1, t3), solver.equality(s, t1), solver.equality(s, t3) });
    solver.addClause({ !solver.equality(t2, t3), solver.equality(s, t2), solver.equality(s, t3) });

    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    solver.decideTrue(solver.equality(s, t1));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    solver.decideTrue(solver.equality(s, t2));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());

    EXPECT_TRUE(solver.analyzeConflicts());
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());

    EXPECT_TRUE(solver.analyzeConflicts());
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    solver.decideTrue(solver.equality(s, t2));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());

    EXPECT_TRUE(solver.analyzeConflicts());
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    solver.decideTrue(solver.equality(s, t3));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());

    EXPECT_TRUE(solver.analyzeConflicts());
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());

    EXPECT_FALSE(solver.analyzeConflicts());
}

TEST(Check, DisequalityOfParentAppliesToNewEdgeAddedOnChild) {
    Solver solver;
    TestKindTheory kind(solver);
    TestValueTheory values(solver);
    Value v1 = values.newValue(solver);
    Value v2 = values.newValue(solver);
    Value v3 = values.newValue(solver);
    solver.propagate();

    solver.decideTrue(solver.equality(v1, v2));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    solver.decideTrue(!solver.equality(v1, v3));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    BooleanValue e23 = solver.equality(v2, v3);
    EXPECT_TRUE(solver.tentativelyTrue(solver.negate(e23)));
    solver.propagate();
    EXPECT_TRUE(solver.assignedFalse(e23));
}

TEST(Check, OutOfOrderRevertedDisequalities) {
    Solver solver;
    TestKindTheory kind(solver);
    TestValueTheory values(solver);
    Value v1 = values.newValue(solver);
    Value v2 = values.newValue(solver);
    Value v3 = values.newValue(solver);
    solver.propagate();

    BooleanValue e13 = solver.equality(v1, v3);
    BooleanValue e23 = solver.equality(v2, v3);

    solver.decideTrue(solver.equality(v1, v2));
    solver.propagate();

    // assign v1 != v3
    solver.decideTrue(solver.negate(e13));
    solver.propagate();
    EXPECT_TRUE(solver.assignedFalse(e13));
    EXPECT_TRUE(solver.assignedFalse(e23));

    // assign v2 != v3
    solver.addClause({ solver.negate(e23) });
    solver.propagate();
    EXPECT_TRUE(solver.assignedFalse(e13));
    EXPECT_TRUE(solver.assignedFalse(e23));

    // revert v1 != v3
    solver.backtrack(solver.currentDecisionLevel());
    solver.propagate();

    // check v2 != v3 still holds
    EXPECT_TRUE(solver.assignedFalse(e13));
    EXPECT_TRUE(solver.assignedFalse(e23));
}

TEST(Check, DisequalityCleanedUpInParents) {
    Solver solver;
    TestKindTheory kind(solver);
    TestValueTheory values(solver);
    Value v1 = values.newValue(solver);
    Value v2 = values.newValue(solver);
    Value v3 = values.newValue(solver);
    Value v4 = values.newValue(solver);
    solver.propagate();

    solver.decideTrue(!solver.equality(v3, v4));
    solver.propagate();

    solver.decideTrue(solver.equality(v1, v3));
    solver.propagate();

    solver.decideTrue(solver.equality(v2, v4));
    solver.propagate();

    solver.backtrack(0);
    solver.propagate();

    BooleanValue e12 = solver.equality(v1, v2);
    solver.propagate();
    EXPECT_FALSE(solver.assignedFalse(e12));
}

struct TestBlockTheory : CodeBlockTheory, BooleanVariables {
    TestBlockTheory(Solver& solver)
        : CodeBlockTheory(solver), BooleanVariables(solver) { }

    BlockId newBlock(Solver& solver) {
        int_t id = variableCount(solver);
        newVariable(solver);
        return BlockId { (uint32_t)CodeBlockTheory::theoryId(), (uint32_t)id };
    }

    std::string formatBlockName(Solver&, BlockId block) override {
        return "test" + std::to_string(block.blockId);
    }
    std::string formatCodePosition(Solver& solver, CodePosition pos) override {
        return formatBlockName(solver, pos.block);
    }
    uint64_t labelOfBlock(Solver&, BlockId block) override {
        return 500 + block.blockId;
    }
    Value loadAtEndOfBlock(Solver& solver, MemoryLocation loc, BlockId block) override {
        return loadAtPosition(solver, loc, { block, 0 });
    }
    Value loadAtPosition(Solver& solver, MemoryLocation loc, CodePosition pos) override {
        return solver.defineLoad(loc, pos);
    }
    BooleanValue blockActiveLiteral(Solver&, BlockId block) override {
        return positiveLiteral(block.blockId);
    }

    std::string formatPositiveLiteral(Solver& solver, int_t varId) override {
        return "active(" + formatBlockName(solver, { (uint32_t)CodeBlockTheory::theoryId(), (uint32_t)varId }) + ")";
    }
    std::string formatNegativeLiteral(Solver& solver, int_t varId) override {
        return "!" + formatPositiveLiteral(solver, varId);
    }
};

TEST(Check, OneOf) {
    Solver solver;
    Phis theory(solver);
    BooleanVariables bools(solver);
    TestBlockTheory blocks(solver);

    solver.propagate();
    std::vector parents { blocks.newBlock(solver), blocks.newBlock(solver), blocks.newBlock(solver) };
    BlockId phi = theory.newPhi(solver, {}, parents);
    solver.decideTrue(theory.blockActiveLiteral(solver, phi));
    solver.propagate();

    auto active0 = theory.linkActiveLiteral(solver, phi, 0);
    auto active1 = theory.linkActiveLiteral(solver, phi, 1);
    auto active2 = theory.linkActiveLiteral(solver, phi, 2);

    solver.decideTrue(active0);
    solver.propagate();
    EXPECT_TRUE(theory.hasActiveLink(phi));
    EXPECT_EQ(theory.activeLink(phi), 0);
    EXPECT_TRUE(solver.assignedFalse(active1));
    EXPECT_TRUE(solver.assignedFalse(active2));

    solver.backtrack(1);
    solver.propagate();
    EXPECT_FALSE(theory.hasActiveLink(phi));

    solver.decideTrue(solver.negate(active0));
    solver.propagate();
    solver.decideTrue(solver.negate(active1));
    solver.propagate();
    EXPECT_TRUE(theory.hasActiveLink(phi));
    EXPECT_EQ(theory.activeLink(phi), 2);

    solver.backtrack(1);
    EXPECT_FALSE(theory.hasActiveLink(phi));

    int_t bVar = bools.newVariable(solver);
    solver.addClause({ bools.positiveLiteral(bVar), active0 });
    solver.addClause({ bools.positiveLiteral(bVar), active1 });
    solver.decideTrue(bools.negativeLiteral(bVar));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
}

struct TestTypes : TypeTheory {
    TestTypes(Solver& solver)
        : TypeTheory(solver), baseLabel(solver, ValueCategory::Literal) { }
    std::optional<ValueKind> scalarKind(Solver&, Type v) override {
        if (v == boolType())
            return ValueKind::Boolean;
        if (v == ptrToBoolType())
            return ValueKind::MemoryLocation;
        VERIFY_NOT_REACHED();
    }
    std::string formatValue(Solver&, Value v) override {
        if (v == boolType())
            return "bool";
        if (v == ptrToBoolType())
            return "ptr{bool}";
        VERIFY_NOT_REACHED();
    }
    uint64_t labelOfValue(Solver&, Value v) override { return baseLabel + v.valueId; }
    std::optional<Type> dereferencedType(Solver&, Type type) override {
        if (type == ptrToBoolType())
            return boolType();
        return std::nullopt;
    }
    std::optional<Type> memberExpressionMemberType(Solver&, Type) override { return std::nullopt; }
    std::optional<Type> memberExpressionBaseType(Solver&, Type) override { return std::nullopt; }

    Type boolType() { return { (uint32_t)theoryId(), 0 }; }
    Type ptrToBoolType() { return { (uint32_t)theoryId(), 1 }; }

    ValueBaseLabel baseLabel;
};

TEST(Check, Code) {
    Solver solver;
    StoreBlocks stores(solver);
    Phis phis(solver);
    SimpleVariables variables(solver);
    TestTypes types(solver);
    solver.propagate();
    auto loc = variables.declareVariable(solver, types.boolType(), { builtins::entry_block, 0 });

    BooleanValue initialLoad = (BooleanValue)solver.loadAtEndOfBlock(loc, builtins::entry_block);

    auto s = stores.newBlock(solver, 1, builtins::entry_block);
    stores.appendStore(solver, s, loc, builtins::true_literal);

    auto p = phis.newPhi(solver, 2, { builtins::entry_block, s });
    solver.addClause({ phis.linkInactiveLiteral(solver, p, 0), initialLoad });
    solver.addClause({ phis.linkInactiveLiteral(solver, p, 1), solver.negate(initialLoad) });

    solver.decideTrue(solver.blockActiveLiteral(p));
    solver.propagate();
    ASSERT_FALSE(solver.hasConflicts());

    BooleanValue finalLoad = (BooleanValue)solver.loadAtEndOfBlock(loc, p);
    solver.decideTrue(solver.negate(finalLoad));
    solver.propagate();
    ASSERT_FALSE(solver.hasConflicts());

    solver.decideTrue(phis.linkActiveLiteral(solver, p, 0));
    solver.propagate();
    ASSERT_TRUE(solver.hasConflicts());

    ASSERT_TRUE(solver.analyzeConflicts());
    solver.propagate();
    ASSERT_TRUE(solver.hasConflicts());

    ASSERT_TRUE(solver.analyzeConflicts());
    solver.propagate();
    ASSERT_TRUE(solver.assignedTrue(finalLoad));
}

TEST(Check, DISABLED_Declaration) {
    Solver solver;
    StoreBlocks stores(solver);
    Phis phis(solver);
    TestTypes types(solver);
    SimpleVariables variables(solver);
    solver.propagate();

    BlockId s = stores.newBlock(solver, 1, builtins::entry_block);
    MemoryLocation ptrLoc = variables.declareVariable(solver, types.ptrToBoolType(), { s, 0 });

    MemoryLocation boolLoc = variables.declareVariable(solver, types.boolType(), { s, 0 });
    stores.appendStore(solver, s, boolLoc, builtins::true_literal);
    stores.appendStore(solver, s, ptrLoc, boolLoc);

    stores.appendStore(solver, s, MemoryLocation { solver.loadAtEndOfBlock(ptrLoc, s) }, builtins::false_literal);

    solver.decideTrue(solver.negate(BooleanValue { solver.loadAtEndOfBlock(boolLoc, s) }));
}

}