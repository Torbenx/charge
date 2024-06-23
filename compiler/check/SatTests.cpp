#include <check/BooleanVariables.h>
#include <check/SatSolver.h>
#include <types.h>

#include <check/BasicBlock.h>
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

void runTests(std::filesystem::path testDir) {
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
        VERIFY(resultString == expectedResultString);

        count += 1;
    }
    VERIFY(count == 80);
    fmt::println("Passed {} sat tests", count);

    // Equality test
    Solver solver;
    solver.propagate();
    struct TestValueTheory : ValueTheory {
        struct Equality : StandardEquality {
            using StandardEquality::StandardEquality;
            EqualityInfo& equalityInfo(Value v) override {
                return infos[v.valueId];
            }

            std::vector<EqualityInfo> infos;
        };

        TestValueTheory(Solver& solver)
            : ValueTheory(solver), equality(solver) { }

        uint64_t labelOf(Solver&, Value v) override { return baseLabel + v.valueId; }
        std::string formatValue(Solver&, Value v) override { return fmt::format("v{}", v.valueId + 1); }
        Type typeOf(Solver&, Value) override { VERIFY_NOT_REACHED(); }
        void enumerateValues(Solver&, std::function<void(Value)>) override { VERIFY_NOT_REACHED(); }

        Value newValue() {
                Value v { .theoryId = (uint32_t)theoryId(), .valueId = (uint32_t)equality.infos.size() };
                equality.infos.emplace_back(v);
                return v;
            }

        uint64_t baseLabel = 0;
        Equality equality;
    };
    {
        TestValueTheory values(solver);
        Value v1 = values.newValue();
        Value v2 = values.newValue();

        Reason r12 = values.equality.linkToReason({ v1, v2 });
        VERIFY(!values.equality.testReason(solver, r12));

        BooleanValue e12 = values.equality.equality(solver, v1, v2);
        VERIFY(!values.equality.testReason(solver, r12));

        solver.decideTrue(e12);
        VERIFY(!values.equality.testReason(solver, r12));

        solver.propagate();
        VERIFY(values.equality.testReason(solver, r12));

        auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r12);
        VERIFY(clause.size() == 2);
        VERIFY(forcedIndex == 0);
        VERIFY(clause[0] == e12);
        VERIFY(clause[1] == values.equality.negate(e12));
    }
    {
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

        VERIFY(solver.assignedTrue(e23));
        Reason r23 = values.equality.linkToReason({ v2, v3 });
        VERIFY(values.equality.testReason(solver, r23));

        auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r23);
        VERIFY(clause.size() == 3);
        VERIFY(clause[forcedIndex] == e23);
        VERIFY(std::find(clause.begin(), clause.end(), values.equality.negate(e12)) != clause.end());
        VERIFY(std::find(clause.begin(), clause.end(), values.equality.negate(e13)) != clause.end());
    }
    {
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

        VERIFY(solver.assignedTrue(e12));
        Reason r12 = values.equality.linkToReason({ v1, v2 });
        VERIFY(values.equality.testReason(solver, r12));

        auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r12);
        VERIFY(clause.size() == 3);
        VERIFY(clause[forcedIndex] == e12);
        VERIFY(std::find(clause.begin(), clause.end(), values.equality.negate(e13)) != clause.end());
        VERIFY(std::find(clause.begin(), clause.end(), values.equality.negate(e23)) != clause.end());
    }
    {
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
        Reason r00_03 = values.equality.linkToReason({ vals[0][0], vals[0][3] });
        VERIFY(values.equality.testReason(solver, r00_03));
        {
            auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r00_03);
            VERIFY(clause.size() == 4);
            VERIFY(clause[forcedIndex] == values.equality.equality(solver, vals[0][0], vals[0][3]));
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][0], vals[0][1])) != clause.end());
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][1], vals[0][2])) != clause.end());
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][2], vals[0][3])) != clause.end());
        }

        for (int_t i = 0; i < 4; i++) {
            solver.decideTrue(values.equality.equality(solver, vals[2][i], vals[3][i]));
            solver.propagate();
            solver.decideTrue(values.equality.equality(solver, vals[1][i], vals[2][i]));
            solver.propagate();
            solver.decideTrue(values.equality.equality(solver, vals[0][i], vals[1][i]));
            solver.propagate();
        }
        Reason r30_33 = values.equality.linkToReason({ vals[3][0], vals[3][3] });
        VERIFY(values.equality.testReason(solver, r30_33));
        {
            auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r30_33);
            VERIFY(clause.size() == 10);
            VERIFY(clause[forcedIndex] == values.equality.equality(solver, vals[3][0], vals[3][3]));

            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][0], vals[1][0])) != clause.end());
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[1][0], vals[2][0])) != clause.end());
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[2][0], vals[3][0])) != clause.end());

            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][0], vals[0][1])) != clause.end());
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][1], vals[0][2])) != clause.end());
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][2], vals[0][3])) != clause.end());

            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][3], vals[1][3])) != clause.end());
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[1][3], vals[2][3])) != clause.end());
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[2][3], vals[3][3])) != clause.end());
        }

        Reason r32_33 = values.equality.linkToReason({ vals[3][2], vals[3][3] });
        {
            auto [clause, forcedIndex] = values.equality.reasonToClause(solver, r32_33);
            VERIFY(clause.size() == 8);
            VERIFY(clause[forcedIndex] == values.equality.equality(solver, vals[3][2], vals[3][3]));

            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][2], vals[1][2])) != clause.end());
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[1][2], vals[2][2])) != clause.end());
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[2][2], vals[3][2])) != clause.end());

            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][2], vals[0][3])) != clause.end());

            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[0][3], vals[1][3])) != clause.end());
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[1][3], vals[2][3])) != clause.end());
            VERIFY(std::find(clause.begin(), clause.end(), values.equality.disequality(solver, vals[2][3], vals[3][3])) != clause.end());
        }
    }

    // TreeLabel
    {
        TreeLabel root = TreeLabel::rootLabel();
        VERIFY(root.label() == 0b0111'1111'1111'1111'1111'1111'1111'1111u);

        VERIFY(root.extend(false).label() == 0b0011'1111'1111'1111'1111'1111'1111'1111u);
        VERIFY(root.extend(false).extend(false).label() == 0b0001'1111'1111'1111'1111'1111'1111'1111u);
        VERIFY(root.extend(false).extend(true).label() == 0b0101'1111'1111'1111'1111'1111'1111'1111u);

        VERIFY(root.extend(true).label() == 0b1011'1111'1111'1111'1111'1111'1111'1111u);
        VERIFY(root.extend(true).extend(false).label() == 0b1001'1111'1111'1111'1111'1111'1111'1111u);
        VERIFY(root.extend(true).extend(true).label() == 0b1101'1111'1111'1111'1111'1111'1111'1111u);

        VERIFY(root.depth() == 0);

        VERIFY(root.extend(false).depth() == 1);
        VERIFY(root.extend(false).extend(false).depth() == 2);
        VERIFY(root.extend(false).extend(true).depth() == 2);

        VERIFY(root.extend(true).depth() == 1);
        VERIFY(root.extend(true).extend(false).depth() == 2);
        VERIFY(root.extend(true).extend(true).depth() == 2);
    }
}

}