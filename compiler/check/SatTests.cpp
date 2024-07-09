#include <check/BooleanVariables.h>
#include <check/SatSolver.h>
#include <gtest/gtest.h>
#include <types.h>

// #include <check/BasicBlock.h>
#include <check/BooleanEquality.h>
#include <check/EqualityTheory.h>
#include <check/StandardEquality.h>
#include <check/TopologicalOrder.h>

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
        VERIFY(theory.newVariable() == 0);
        for (int_t varId = 1; varId <= parser.variableCount; varId++)
            VERIFY(theory.newVariable() == varId);
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
            auto var = theory.findUnassignedVariable();
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
            if (theory.literalInfo(theory.positiveLiteral(varId)).assignedFalse())
                assignment.push_back(false);
            else if (theory.literalInfo(theory.negativeLiteral(varId)).assignedFalse())
                assignment.push_back(true);
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

struct TestValueTheory : EquatableValueTheory {
    TestValueTheory(Solver& solver)
        : EquatableValueTheory(solver), equality(solver) { }

    uint64_t labelOf(Solver&, Value v) override { return baseLabel + v.valueId; }
    std::string formatValue(Solver&, Value v) override { return fmt::format("v{}", v.valueId + 1); }
    Type typeOf(Solver&, Value) override { VERIFY_NOT_REACHED(); }
    void enumerateValues(Solver&, std::function<void(Value)>) override { VERIFY_NOT_REACHED(); }

    Value newValue() {
        Value v { .theoryId = (uint32_t)theoryId(), .valueId = (uint32_t)infos.size() };
        infos.emplace_back(v);
        return v;
    }

    EqualityInfo& equalityInfo(Solver&, Value v) override { return infos[v.valueId]; }
    void propagateEquality(Solver&, Value, Value) override { }

    Reason equalityReason(Solver& solver, Value v1, Value v2) {
        return equality.equalityReason(equality.variableId(equality.equality(solver, v1, v2)));
    }

    uint64_t baseLabel = 0;
    StandardEquality equality;
    std::vector<EqualityInfo> infos;
};

TEST(Check, EqualityTreePath1) {
    Solver solver;
    solver.propagate();
    TestValueTheory values(solver);
    Value v1 = values.newValue();
    Value v2 = values.newValue();

    Reason r12 = values.equalityReason(solver, v1, v2);
    EXPECT_FALSE(values.equality.testReason(solver, r12));

    BooleanValue e12 = values.equality.equality(solver, v1, v2);
    EXPECT_FALSE(values.equality.testReason(solver, r12));

    solver.decideTrue(e12);
    EXPECT_FALSE(values.equality.testReason(solver, r12));

    solver.propagate();
    EXPECT_TRUE(values.equality.testReason(solver, r12));

    {
        auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r12);
        EXPECT_EQ(clause.size(), 2);
        EXPECT_EQ(forcedIndex, 0);
        EXPECT_EQ(clause[0], e12);
        EXPECT_EQ(clause[1], values.equality.negate(e12));
    }

    solver.backtrack(0);
    {
        auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r12);
        EXPECT_EQ(clause.size(), 2);
        EXPECT_EQ(forcedIndex, 0);
        EXPECT_EQ(clause[0], e12);
        EXPECT_EQ(clause[1], values.equality.negate(e12));
    }
}

TEST(Check, EqualityTreePath2) {
    Solver solver;
    solver.propagate();
    TestValueTheory values(solver);
    Value v1 = values.newValue();
    Value v2 = values.newValue();
    Value v3 = values.newValue();

    BooleanValue e12 = values.equality.equality(solver, v1, v2);
    BooleanValue e13 = values.equality.equality(solver, v1, v3);
    BooleanValue e23 = values.equality.equality(solver, v2, v3);
    solver.decideTrue(e12);
    solver.propagate();
    solver.decideTrue(e13);
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(e23));
    Reason r23 = values.equalityReason(solver, v2, v3);
    EXPECT_TRUE(values.equality.testReason(solver, r23));

    auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r23);
    EXPECT_EQ(clause.size(), 3);
    EXPECT_EQ(clause[forcedIndex], e23);
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.negate(e12)) != clause.end());
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.negate(e13)) != clause.end());
}

TEST(Check, EqualityTreePath3) {
    Solver solver;
    solver.propagate();
    TestValueTheory values(solver);
    Value v1 = values.newValue();
    Value v2 = values.newValue();
    Value v3 = values.newValue();

    BooleanValue e13 = values.equality.equality(solver, v1, v3);
    BooleanValue e23 = values.equality.equality(solver, v2, v3);
    BooleanValue e12 = values.equality.equality(solver, v1, v2);
    solver.decideTrue(e13);
    solver.propagate();
    solver.decideTrue(e23);
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(e12));
    Reason r12 = values.equalityReason(solver, v1, v2);
    EXPECT_TRUE(values.equality.testReason(solver, r12));

    auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r12);
    EXPECT_EQ(clause.size(), 3);
    EXPECT_EQ(clause[forcedIndex], e12);
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.negate(e13)) != clause.end());
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.negate(e23)) != clause.end());
}

TEST(Check, EqualityTreePath4) {
    Solver solver;
    solver.propagate();
    TestValueTheory values(solver);
    Value vals[4][4];
    for (int_t i = 0; i < 4; i++) {
        for (int_t j = 0; j < 4; j++)
            vals[i][j] = values.newValue();
    }

    solver.decideTrue(values.equality.equality(solver, vals[0][0], vals[0][1]));
    solver.propagate();
    solver.decideTrue(values.equality.equality(solver, vals[0][1], vals[0][2]));
    solver.propagate();
    solver.decideTrue(values.equality.equality(solver, vals[0][2], vals[0][3]));
    solver.propagate();
    Reason r00_03 = values.equalityReason(solver, vals[0][0], vals[0][3]);
    EXPECT_TRUE(values.equality.testReason(solver, r00_03));
    {
        auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r00_03);
        EXPECT_EQ(clause.size(), 4);
        EXPECT_EQ(clause[forcedIndex], values.equality.equality(solver, vals[0][0], vals[0][3]));
        EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][0], vals[0][1])) != clause.end());
        EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][1], vals[0][2])) != clause.end());
        EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][2], vals[0][3])) != clause.end());
    }
    solver.propagate();

    for (int_t i = 0; i < 4; i++) {
        solver.decideTrue(values.equality.equality(solver, vals[2][i], vals[3][i]));
        solver.propagate();
        solver.decideTrue(values.equality.equality(solver, vals[1][i], vals[2][i]));
        solver.propagate();
        solver.decideTrue(values.equality.equality(solver, vals[0][i], vals[1][i]));
        solver.propagate();
    }

    Reason r30_33 = values.equalityReason(solver, vals[3][0], vals[3][3]);
    EXPECT_TRUE(values.equality.testReason(solver, r30_33));
    Reason r32_33 = values.equalityReason(solver, vals[3][2], vals[3][3]);
    EXPECT_TRUE(values.equality.testReason(solver, r32_33));

    auto testConnections = [&] {
        {
            auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r30_33);
            EXPECT_EQ(clause.size(), 10);
            EXPECT_EQ(clause[forcedIndex], values.equality.equality(solver, vals[3][0], vals[3][3]));

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][0], vals[1][0])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[1][0], vals[2][0])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[2][0], vals[3][0])) != clause.end());

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][0], vals[0][1])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][1], vals[0][2])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][2], vals[0][3])) != clause.end());

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][3], vals[1][3])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[1][3], vals[2][3])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[2][3], vals[3][3])) != clause.end());
        }

        {
            auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r32_33);
            EXPECT_EQ(clause.size(), 8);
            EXPECT_EQ(clause[forcedIndex], values.equality.equality(solver, vals[3][2], vals[3][3]));

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][2], vals[1][2])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[1][2], vals[2][2])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[2][2], vals[3][2])) != clause.end());

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][2], vals[0][3])) != clause.end());

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][3], vals[1][3])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[1][3], vals[2][3])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[2][3], vals[3][3])) != clause.end());
        }
    };
    testConnections();

    solver.backtrack(0);
    EXPECT_FALSE(values.equality.testReason(solver, r30_33));
    EXPECT_FALSE(values.equality.testReason(solver, r32_33));

    testConnections();
}

TEST(Check, EqualityPropagation1) {
    Solver solver;
    TestValueTheory values(solver);
    Value v1 = values.newValue();
    Value v2 = values.newValue();
    Value v3 = values.newValue();
    solver.addClause({ values.equality.equality(solver, v1, v2) });
    solver.addClause({ values.equality.equality(solver, v2, v3) });
    solver.addClause({ values.equality.disequality(solver, v1, v3) });
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
}

TEST(Check, EqualityPropagation2) {
    Solver solver;
    TestValueTheory values(solver);
    BooleanVariables bools(solver);
    BooleanValue c = bools.positiveLiteral(bools.newVariable());
    Value s = values.newValue();
    Value t1 = values.newValue();
    Value t2 = values.newValue();
    solver.addClause({ c, values.equality.equality(solver, s, t1), values.equality.equality(solver, s, t2) });
    solver.addClause({ solver.negate(c) });
    solver.addClause({ values.equality.equality(solver, t1, t2) });
    solver.addClause({ values.equality.disequality(solver, s, t1) });
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
}

TEST(Check, DisequalityPropagation1) {
    Solver solver;
    TestValueTheory values(solver);
    BooleanVariables bools(solver);
    BooleanValue c = bools.positiveLiteral(bools.newVariable());
    Value s = values.newValue();
    Value t1 = values.newValue();
    Value t2 = values.newValue();
    solver.addClause({ c, values.equality.equality(solver, s, t1), values.equality.equality(solver, s, t2) });
    solver.addClause({ values.equality.disequality(solver, s, t1) });
    solver.addClause({ values.equality.equality(solver, t1, t2) });
    solver.propagate();
    VERIFY(solver.assignedTrue(c));
}

TEST(Check, DisequalityPropagation2) {
    Solver solver;
    TestValueTheory values(solver);
    BooleanVariables bools(solver);
    BooleanValue c = bools.positiveLiteral(bools.newVariable());
    Value s = values.newValue();
    Value t1 = values.newValue();
    Value t2 = values.newValue();
    solver.addClause({ c, values.equality.equality(solver, s, t1), values.equality.equality(solver, s, t2) });
    solver.addClause({ values.equality.equality(solver, t1, t2) });
    solver.addClause({ values.equality.disequality(solver, s, t1) });
    solver.propagate();
    VERIFY(solver.assignedTrue(c));
}

TEST(Check, EqualityProblem) {
    Solver solver;
    TestValueTheory values(solver);
    Value s = values.newValue();
    Value t1 = values.newValue();
    Value t2 = values.newValue();
    Value t3 = values.newValue();

    solver.addClause({ values.equality.disequality(solver, s, t1), values.equality.disequality(solver, s, t2), values.equality.equality(solver, s, t3) });
    solver.addClause({ values.equality.disequality(solver, s, t1), values.equality.equality(solver, s, t2), values.equality.disequality(solver, s, t3) });
    solver.addClause({ values.equality.equality(solver, s, t1), values.equality.disequality(solver, s, t2), values.equality.disequality(solver, s, t3) });

    solver.addClause({ values.equality.disequality(solver, t1, t2), values.equality.disequality(solver, t1, t3) });
    solver.addClause({ values.equality.equality(solver, t1, t2), values.equality.equality(solver, t1, t3), values.equality.equality(solver, t2, t3) });

    solver.addClause({ values.equality.disequality(solver, t1, t2), values.equality.equality(solver, s, t1), values.equality.equality(solver, s, t2)  });
    solver.addClause({ values.equality.disequality(solver, t1, t3), values.equality.equality(solver, s, t1), values.equality.equality(solver, s, t3)  });
    solver.addClause({ values.equality.disequality(solver, t2, t3), values.equality.equality(solver, s, t2), values.equality.equality(solver, s, t3)  });

    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    solver.decideTrue(values.equality.equality(solver, s, t1));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    solver.decideTrue(values.equality.equality(solver, s, t2));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());

    EXPECT_TRUE(solver.analyzeConflicts());
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());

    EXPECT_TRUE(solver.analyzeConflicts());
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    solver.decideTrue(values.equality.equality(solver, s, t2));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());

    EXPECT_TRUE(solver.analyzeConflicts());
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    solver.decideTrue(values.equality.equality(solver, s, t3));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());

    EXPECT_TRUE(solver.analyzeConflicts());
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());

    EXPECT_FALSE(solver.analyzeConflicts());
}

TEST(Check, TreeLabel) {
    TreeLabel root = TreeLabel::rootLabel();
    EXPECT_EQ(root.label(), 0b0111'1111'1111'1111'1111'1111'1111'1111u);

    EXPECT_EQ(root.extend(false).label(), 0b0011'1111'1111'1111'1111'1111'1111'1111u);
    EXPECT_EQ(root.extend(false).extend(false).label(), 0b0001'1111'1111'1111'1111'1111'1111'1111u);
    EXPECT_EQ(root.extend(false).extend(true).label(), 0b0101'1111'1111'1111'1111'1111'1111'1111u);

    EXPECT_EQ(root.extend(true).label(), 0b1011'1111'1111'1111'1111'1111'1111'1111u);
    EXPECT_EQ(root.extend(true).extend(false).label(), 0b1001'1111'1111'1111'1111'1111'1111'1111u);
    EXPECT_EQ(root.extend(true).extend(true).label(), 0b1101'1111'1111'1111'1111'1111'1111'1111u);

    EXPECT_EQ(root.depth(), 0);

    EXPECT_EQ(root.extend(false).depth(), 1);
    EXPECT_EQ(root.extend(false).extend(false).depth(), 2);
    EXPECT_EQ(root.extend(false).extend(true).depth(), 2);

    EXPECT_EQ(root.extend(true).depth(), 1);
    EXPECT_EQ(root.extend(true).extend(false).depth(), 2);
    EXPECT_EQ(root.extend(true).extend(true).depth(), 2);
}

/*TEST(Check, BasicBlocks) {
    Solver solver;
    BlockManager manager(solver);
    TestValueTheory values(solver);
    Loads loads(solver);

    MemoryLocation x = {}; // TODO
    Value v1 = values.newValue();
    Value v2 = values.newValue();
    solver.addClause({ values.equality.disequality(solver, v1, v2) });
    manager.store(x, v1);
    manager.branch(values.equality.disequality(solver, loads.load(x), v1), [&]() { manager.proveUnreachable(); });
    manager.branch(values.equality.equality(solver, loads.load(x), v2), [&]() { manager.proveUnreachable(); });
}*/

}