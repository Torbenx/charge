#include <check.h>

#include <filesystem>
#include <fstream>

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

namespace check {

struct BoolTheory : Theory {
    int_t thisTheoryId = -1;
    std::vector<LiteralInfo> infos;
    int_t find = 0;

    std::string format(Literal lit) override {
        int_t varId = lit.literalId >> 1;
        return std::to_string((lit.literalId & 1u) == 0 ? varId : -varId);
    }

    Literal negate(Literal lit) override {
        return { lit.theoryId, lit.literalId ^ 1u };
    }

    const LiteralInfo* getInfo(Literal lit) override {
        return &infos[lit.literalId];
    }

    void assignFalse(Literal lit, int_t tracePos, std::optional<LiteralInstance> clause) override {
        infos[lit.literalId].assignFalse(tracePos, clause);
    }

    void reverseFalseAssignment(Literal lit) override {
        VERIFY(infos[lit.literalId].assignedFalse());
        infos[lit.literalId].reverseFalseAssignment();
    }

    void setTheoryId(int_t id) override {
        thisTheoryId = id;
    }

    void addLiteralInstance(Literal lit, LiteralInstance inst) override {
        infos[lit.literalId].instances.push_back(inst);
    }

    int_t newVariable() {
        int_t id = infos.size() / 2;
        infos.resize(infos.size() + 2);
        return id;
    }

    std::optional<int_t> findUnassignedVariable() {
        for (int_t i = find; i < (int_t)infos.size() / 2; i++) {
            if (getInfo(positiveLiteral(i))->assignedFalse() || getInfo(negativeLiteral(i))->assignedFalse())
                continue;
            find = i;
            return i;
        }
        for (int_t i = 0; i < find; i++) {
            if (getInfo(positiveLiteral(i))->assignedFalse() || getInfo(negativeLiteral(i))->assignedFalse())
                continue;
            find = i;
            return i;
        }
        return std::nullopt;
    }

    Literal positiveLiteral(int_t varId) const { return { (uint32_t)thisTheoryId, (uint32_t)varId * 2u }; }
    Literal negativeLiteral(int_t varId) const { return { (uint32_t)thisTheoryId, (uint32_t)varId * 2u + 1u }; }
    Literal literalFromSign(int_t var) const { return var < 0 ? negativeLiteral(-var) : positiveLiteral(var); }
};

void check(const Parser& parser) {
    // setup
    check::Solver solver;
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

    // solver
    bool unsat = false;
    if (!solver.propagate())
        unsat = true;

    while (!unsat) {
        auto var = theory->findUnassignedVariable();
        if (!var.has_value())
            break;

        int_t varId = var.value();
        VERIFY(solver.decideTrue(theory->positiveLiteral(varId)));
        while (!solver.propagate())
            solver.learnClause();
    }

    if (unsat) {
        std::cout << "unsat\n";
    } else {
        std::cout << "sat\n";
        for (int_t varId = 1; varId <= parser.variableCount; varId++) {
            if (theory->getInfo(theory->positiveLiteral(varId))->assignedFalse())
                fmt::println("{} = false", varId);
            else if (theory->getInfo(theory->negativeLiteral(varId))->assignedFalse())
                fmt::println("{} = true", varId);
            else
                VERIFY_NOT_REACHED();
        }
        solver.checkAssignment();
    }
}

}

static auto readFile(std::filesystem::path file) {
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

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    fs::path testDir { CHECK_TEST_DIR };
    std::string filter;
    if (argc > 1) {
        VERIFY(argc == 2);
        filter = argv[1];
    }
    //try {
        for (const auto& entry : fs::directory_iterator(testDir)) {
            if (!entry.is_regular_file())
                continue;
            if (!entry.path().filename().string().starts_with(filter))
                continue;

            auto sourceBuffer = readFile(entry.path());
            fmt::println("{}", entry.path().filename().string());
            Parser parser;
            parser.buffer = sourceBuffer;
            parser.parse();
            check::check(parser);
        }
    //} catch (...) { }
}