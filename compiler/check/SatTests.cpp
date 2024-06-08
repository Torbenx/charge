#include <check/SatBoolTheory.h>
#include <check/SatSolver.h>
#include <types.h>

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

    bool check(const Parser& parser) {
        // setup
        Solver solver;
        int_t boolId = solver.addTheory(std::make_unique<BoolTheory>());
        VERIFY(boolId == 0);
        BoolTheory* theory = static_cast<BoolTheory*>(solver.getTheoryById(0));

        // generate
        VERIFY(theory->newVariable() == 0);
        for (int_t varId = 1; varId <= parser.variableCount; varId++)
            VERIFY(theory->newVariable() == varId);
        for (const auto& clause : parser.cnf) {
            std::vector<Literal> outClause;
            for (int_t i = 0; i < (int_t)clause.size(); i++)
                outClause.push_back(theory->literalFromSign(clause[i]));
            solver.addClause(std::move(outClause));
        }

        if (solver.hasConflicts() || !solver.propagate())
            return false; // unsat

        // solver
        for (;;) {
            auto var = theory->findUnassignedVariable();
            if (!var.has_value())
                break;

            int_t varId = var.value();
            VERIFY(solver.decideTrue(theory->negativeLiteral(varId)));
            while (!solver.propagate()) {
                if (!solver.analyzeConflicts())
                    return false; // unsat
            }
        }

        // sat
        /*for (int_t varId = 1; varId <= parser.variableCount; varId++) {
            if (theory->getInfo(theory->positiveLiteral(varId))->assignedFalse())
                fmt::println("{} = false", varId);
            else if (theory->getInfo(theory->negativeLiteral(varId))->assignedFalse())
                fmt::println("{} = true", varId);
            else
                VERIFY_NOT_REACHED();
        }*/
        VERIFY(solver.checkAssignment());
        return true;
    }

    std::string readFile(std::filesystem::path file) {
        std::ifstream stream;
        stream.open(file, std::ios::binary);
        VERIFY(stream.good());
        stream.seekg(0, std::ios::end);
        int_t length = stream.tellg();
        VERIFY(length >= 0);
        std::string sourceBuffer;
        sourceBuffer.resize(length + 2);
        stream.seekg(0, std::ios::beg);
        stream.read(sourceBuffer.data(), length);
        stream.close();
        VERIFY(stream.good());

        return sourceBuffer;
    }
}

void runTests(std::filesystem::path testDir) {

    static const std::unordered_map<std::string, bool> expectedResults = {
        { "add16.cnf", false },
        { "add32.cnf", false },
        { "add4.cnf", false },
        { "add64.cnf", false },
        { "add8.cnf", false },
        { "aim-100-1_6-no-1.cnf", false },
        { "aim-50-1_6-yes1-4.cnf", true },
        { "block0.cnf", true },
        { "dubois20.cnf", false },
        { "dubois21.cnf", false },
        { "dubois22.cnf", false },
        { "elimclash.cnf", false },
        { "elimredundant.cnf", true },
        { "empty.cnf", true },
        { "factor1234321.cnf", true },
        { "factor2708413neg.cnf", true },
        { "full1.cnf", false },
        { "full2.cnf", false },
        { "full3.cnf", false },
        { "full4.cnf", false },
        { "full5.cnf", false },
        { "full6.cnf", false },
        { "full7.cnf", false },
        { "hole6.cnf", false },
        { "learn.cnf", true },
        { "par8-1-c.cnf", true },
        { "ph2.cnf", false },
        { "ph3.cnf", false },
        { "ph4.cnf", false },
        { "ph5.cnf", false },
        { "ph6.cnf", false },
        { "prime121.cnf", true },
        { "prime1369.cnf", true },
        { "prime1681.cnf", true },
        { "prime169.cnf", true },
        { "prime1849.cnf", true },
        { "prime2209.cnf", true },
        { "prime25.cnf", true },
        { "prime289.cnf", true },
        { "prime361.cnf", true },
        { "prime4.cnf", true },
        { "prime49.cnf", true },
        { "prime529.cnf", true },
        { "prime65537.cnf", false },
        { "prime841.cnf", true },
        { "prime9.cnf", true },
        { "prime961.cnf", true },
        { "quinn.cnf", true },
        { "regr000.cnf", true },
        { "simple_v3_c2.cnf", true },
        { "sqrt10201.cnf", true },
        { "sqrt1042441.cnf", true },
        { "sqrt10609.cnf", true },
        { "sqrt11449.cnf", true },
        { "sqrt11881.cnf", true },
        { "sqrt12769.cnf", true },
        { "sqrt16129.cnf", true },
        { "sqrt259081.cnf", true },
        { "sqrt2809.cnf", true },
        { "sqrt3481.cnf", true },
        { "sqrt3721.cnf", true },
        { "sqrt4489.cnf", true },
        { "sqrt5041.cnf", true },
        { "sqrt5329.cnf", true },
        { "sqrt6241.cnf", true },
        { "sqrt63001.cnf", true },
        { "sqrt6889.cnf", true },
        { "sqrt7921.cnf", true },
        { "sqrt9409.cnf", true },
        { "sub0.cnf", true },
        { "unit0.cnf", true },
        { "unit1.cnf", true },
        { "unit2.cnf", true },
        { "unit3.cnf", true },
        { "unit4.cnf", false },
        { "unit5.cnf", false },
        { "unit6.cnf", false },
        { "unit7.cnf", false },
        { "unsat.cnf", false },
        { "zebra_v155_c1135.cnf", true },
    };

    namespace fs = std::filesystem;
    int_t count = 0;
    for (const auto& entry : fs::directory_iterator(testDir)) {
        if (!entry.is_regular_file())
            continue;

        auto sourceBuffer = readFile(entry.path());
        Parser parser;
        parser.buffer = sourceBuffer;
        parser.parse();
        bool result = check(parser);
        VERIFY(expectedResults.at(entry.path().filename().string()) == result);
        count += 1;
    }
    VERIFY(count == (int_t)expectedResults.size());
    fmt::println("Passed {} sat tests", count);
}

}