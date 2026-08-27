#include <server/Server.h>
#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace verify::backend {

struct CnfParser {
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

static std::optional<std::vector<bool>> check(const CnfParser& parser) {
    // setup
    SolverImpl solver;

    // generate
    VERIFY(solver.newAuxBooleanVariable().id() == 0);
    for (int_t varId = 1; varId <= parser.variableCount; varId++)
        VERIFY(solver.newAuxBooleanVariable().id() == varId * 2);
    for (const auto& clause : parser.cnf) {
        // The clauses of a problem may spell a literal more than once, which the solver does not
        // expect: it identifies the literals of a clause by their position in it. The builder
        // drops the repetitions.
        ClauseBuilder builder = solver.beginClause();
        for (int_t i = 0; i < (int_t)clause.size(); i++) {
            auto lit = Bool(TheoryId::AuxBooleanVariables, std::abs(clause[i]) * 2);
            builder.add(solver, clause[i] > 0 ? lit : !lit);
        }
        solver.addClause(builder);
    }

    GrindResult result = solver.grindDecisions();
    VERIFY(result != GrindResult::AssumptionsUnsatisfiable); // the search began without assumptions
    if (result == GrindResult::UnconditionallyUnsatisfiable)
        return std::nullopt;

    // sat
    std::vector<bool> assignment;
    for (int_t varId = 1; varId <= parser.variableCount; varId++) {
        auto lit = Bool(TheoryId::AuxBooleanVariables, varId * 2);
        if (solver.assignedTrue(lit))
            assignment.push_back(true);
        else if (solver.assignedFalse(lit))
            assignment.push_back(false);
        else
            VERIFY_NOT_REACHED();
    }
    return assignment;
}

static void writeFile(std::filesystem::path file, std::string content) {
    std::ofstream stream;
    stream.open(file, std::ios::binary);
    VERIFY(stream.good());
    stream.write(content.data(), content.length());
    stream.close();
    VERIFY(stream.good());
}

static std::string nameList(const std::vector<std::string>& files) {
    std::string result;
    for (const auto& file : files) {
        if (!result.empty())
            result += ", ";
        result += file;
    }
    return result;
}

TEST(VerifyBackend, SatProblems) {
    std::filesystem::path testDir = COMPILER_TEST_DIR "/sat";
    bool overwriteSolutionFiles = false;

    namespace fs = std::filesystem;
    int_t count = 0;
    std::vector<std::string> wrongResults;
    std::vector<std::string> changedModels;
    for (const auto& entry : fs::directory_iterator(testDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cnf")
            continue;

        auto sourceBuffer = server::readFile(entry.path());
        CnfParser parser;
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

        auto expectedResultString = server::readFile(solutionPath);
        if (resultString != expectedResultString) {
            if (resultString.empty() != expectedResultString.empty())
                wrongResults.push_back(entry.path().filename().string());
            else
                changedModels.push_back(entry.path().filename().string());
        }

        count += 1;
    }
    EXPECT_EQ(count, 83);
    EXPECT_TRUE(wrongResults.empty())
        << "wrong results: " << nameList(wrongResults);
    EXPECT_TRUE(changedModels.empty())
        << "changed models: " << nameList(changedModels);
}

}