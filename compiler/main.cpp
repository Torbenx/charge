#include <parse/Parser.h>
#include <parse/api.h>
#include <sema/Generator.h>
#include <server/MessageLog.h>
#include <server/Server.h>

#define CLI11_ENABLE_EXTRA_VALIDATORS 1

#include <CLI/CLI.hpp>
#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>

#ifdef WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using Clock = std::chrono::high_resolution_clock;

struct PerfEnable {
    static bool perfPresent() {
        PerfEnable enable;
        return enable.ctlFd != 0;
    }

    PerfEnable() {
        char* ctlFdStr = std::getenv("PERF_CTL_FD");
        char* ackFdStr = std::getenv("PERF_ACK_FD");
        if (ctlFdStr != nullptr && ackFdStr != nullptr) {
            ctlFd = std::atoi(ctlFdStr);
            ackFd = std::atoi(ackFdStr);
        }
        if (ctlFd != 0 && ackFd != 0) {
            write(ctlFd, "enable", 7);
            std::array<char, 5> result = {};
            VERIFY(read(ackFd, result.data(), 5) == 5);
        }
    }

    ~PerfEnable() {
        if (ctlFd != 0 && ackFd != 0) {
            write(ctlFd, "disable", 8);
            std::array<char, 5> result = {};
            VERIFY(read(ackFd, result.data(), 5) == 5);
        }
    }

    int ctlFd = 0;
    int ackFd = 0;
};

using LexerOutput = parse::SimpleTokenBuffer<parse::LexerToken>;
using LexerFunction = const char* (*)(const char* sourcePosition, LexerOutput& output);

template<LexerFunction lexFunc>
struct Lexer {
    explicit Lexer(padded_string_view source)
        : sourcePosition(source.begin()) { }

    void parse(LexerOutput& output) {
        sourcePosition = lexFunc(sourcePosition, output);
    }
    bool done() const { return true; }

    const char* sourcePosition;
};

struct NoParser {
    explicit NoParser(padded_string_view) { }
    void parse(parse::NoOutput&) { }
    bool done() const { return true; }
};

template<typename Output>
static Output makeOutput(padded_string_view sourceBuffer) {
    if constexpr (std::same_as<Output, sema::Context>) {
        return Output({}, sourceBuffer);
    } else if constexpr (std::same_as<Output, parse::NoOutput>) {
        return Output();
    } else {
        return Output(sourceBuffer);
    }
}

template<typename Output>
static std::string outputString(const Output& output) {
    if constexpr (std::same_as<Output, sema::Context> || std::same_as<Output, parse::SimpleOutput>)
        return std::format("{} semantic tokens", output.tokenBuffer.tokens.size());
    else if constexpr (std::same_as<Output, parse::NoOutput>)
        return "no output";
    else
        return std::format("{} tokens", output.tokens.size());
}

enum class ClearMethod : uint8_t {
    Reset,
    Realloc,
};

template<typename Parser, typename Output, ClearMethod clear>
struct BenchmarkImpl;

template<typename Parser, typename Output>
struct BenchmarkImpl<Parser, Output, ClearMethod::Reset> {
    Output output;
    padded_string_view sourceBuffer;

    BenchmarkImpl(padded_string_view sourceBuffer)
        : output(makeOutput<Output>(sourceBuffer))
        , sourceBuffer(sourceBuffer) { }

    bool operator()() {
        output.reset();
        Parser parser(sourceBuffer);
        parser.parse(output);
        return parser.done();
    }
};

template<typename Parser, typename Output>
struct BenchmarkImpl<Parser, Output, ClearMethod::Realloc> {
    padded_string_view sourceBuffer;

    BenchmarkImpl(padded_string_view sourceBuffer)
        : sourceBuffer(sourceBuffer) { }

    bool operator()() {
        Output output = makeOutput<Output>(sourceBuffer);
        Parser parser(sourceBuffer);
        parser.parse(output);
        return parser.done();
    }
};

namespace parse {
const char* lexSwitchAndBranch(const char* sourcePosition, LexerOutput& output);
const char* lexTable2Char(const char* sourcePosition, LexerOutput& output);
const char* lexSwitchAndPatternTable(const char* sourcePosition, LexerOutput& output);
const char* lexPatternTable(const char* sourcePosition, LexerOutput& output);
const char* lexTableHybrid(const char* sourcePosition, LexerOutput& output);
const char* lexSwitchAndTable(const char* sourcePosition, LexerOutput& output);
const char* lexExpr1State(const char* sourcePosition, LexerOutput& output);
const char* lexExpr2State(const char* sourcePosition, LexerOutput& output);
}

template<typename Parser, typename Output>
static void withClearMethod(ClearMethod clearMethod, padded_string_view sourceBuffer, auto callback) {
    switch (clearMethod) {
    case ClearMethod::Reset:
        if constexpr (requires(Output& output) { output.reset(); })
            callback(BenchmarkImpl<Parser, Output, ClearMethod::Reset> { sourceBuffer });
        else
            VERIFY_NOT_REACHED();
        break;
    case ClearMethod::Realloc:
        callback(BenchmarkImpl<Parser, Output, ClearMethod::Realloc> { sourceBuffer });
        break;
    default:
        VERIFY_NOT_REACHED();
    }
}

static void withImpl(std::string_view impl, ClearMethod clearMethod, padded_string_view sourceBuffer, auto callback) {
    if (impl == "sema") {
        withClearMethod<parse::Parser, sema::Context>(clearMethod, sourceBuffer, std::move(callback));
    } else if (impl == "simple") {
        withClearMethod<parse::SimpleParser, parse::SimpleOutput>(clearMethod, sourceBuffer, std::move(callback));
    } else if (impl == "no-output") {
        withClearMethod<parse::SimpleParser, parse::NoOutput>(clearMethod, sourceBuffer, std::move(callback));
    } else if (impl == "switch-and-branch") {
        withClearMethod<Lexer<parse::lexSwitchAndBranch>, LexerOutput>(clearMethod, sourceBuffer, std::move(callback));
    } else if (impl == "switch-and-pattern-table") {
        withClearMethod<Lexer<parse::lexSwitchAndPatternTable>, LexerOutput>(clearMethod, sourceBuffer, std::move(callback));
    } else if (impl == "pattern-table") {
        withClearMethod<Lexer<parse::lexPatternTable>, LexerOutput>(clearMethod, sourceBuffer, std::move(callback));
    } else if (impl == "table-2char") {
        withClearMethod<Lexer<parse::lexTable2Char>, LexerOutput>(clearMethod, sourceBuffer, std::move(callback));
    } else if (impl == "table-hybrid") {
        withClearMethod<Lexer<parse::lexTableHybrid>, LexerOutput>(clearMethod, sourceBuffer, std::move(callback));
    } else if (impl == "switch-and-table") {
        withClearMethod<Lexer<parse::lexSwitchAndTable>, LexerOutput>(clearMethod, sourceBuffer, std::move(callback));
    } else if (impl == "expr-1state") {
        withClearMethod<Lexer<parse::lexExpr1State>, LexerOutput>(clearMethod, sourceBuffer, std::move(callback));
    } else if (impl == "expr-2state") {
        withClearMethod<Lexer<parse::lexExpr2State>, LexerOutput>(clearMethod, sourceBuffer, std::move(callback));
    } else if (impl == "baseline") {
        withClearMethod<NoParser, parse::NoOutput>(clearMethod, sourceBuffer, std::move(callback));
    } else {
        VERIFY_NOT_REACHED();
    }
}

int runBenchmark(std::string_view impl, std::string_view file, int repeats, ClearMethod clearMethod) {
    padded_string source = server::readFile(file);
    if (PerfEnable::perfPresent()) {
        withImpl(impl, clearMethod, source, [&](auto bench) {
            PerfEnable enable;
            for (int i = 0; i < repeats; i++) {
                bench();
            }
        });
    } else {
        withImpl(impl, clearMethod, source, [&](auto bench) {
            auto start = Clock::now();
            for (int i = 0; i < repeats; i++) {
                bench();
            }
            auto stop = Clock::now();
            std::cout << "Processing took " << std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(stop - start) / repeats;
            if constexpr (requires { bench.output; })
                std::cout << " and produced " << outputString(bench.output);
            std::cout << "\n";
        });
    }
    return 0;
}

void runGoogleBenchmark(benchmark::State& state, std::string_view file) {
    padded_string source = server::readFile(file);

    bool realloc = state.range(0);
    ClearMethod clearMethod = realloc ? ClearMethod::Realloc : ClearMethod::Reset;
    std::string name = state.name();
    std::string_view impl = std::string_view(name).substr(state.name().find('/') + 1);
    withImpl(impl, clearMethod, source, [&state](auto bench) {
        for (auto _ : state) {
            bench();
        }
    });

    auto bytes = source.size();
    LexerOutput output(source);
    parse::lexSwitchAndBranch(source.data(), output);
    auto lines = output.lines.size();
    auto tokens = output.tokens.size();

    state.counters["bytes"] = benchmark::Counter(bytes, benchmark::Counter::kIsIterationInvariantRate);
    state.counters["lines"] = benchmark::Counter(lines, benchmark::Counter::kIsIterationInvariantRate);
    state.counters["tokens"] = benchmark::Counter(tokens, benchmark::Counter::kIsIterationInvariantRate);
}

void benchmarkImpl(benchmark::State& state) {
    runGoogleBenchmark(state, { COMPILER_TEST_DIR "/../benchmark/benchmark.chrg" });
}

void benchmarkExprImpl(benchmark::State& state) {
    runGoogleBenchmark(state, { COMPILER_TEST_DIR "/../benchmark-expr/benchmark-expr-nocomments.chrg" });
}

// clang-format off
BENCHMARK_NAMED(benchmarkImpl, no-output)->ArgName("realloc")->Arg(0);
BENCHMARK_NAMED(benchmarkImpl, table-hybrid)->ArgName("realloc")->Range(0, 1);
BENCHMARK_NAMED(benchmarkImpl, switch-and-table)->ArgName("realloc")->Range(0, 1);
BENCHMARK_NAMED(benchmarkImpl, switch-and-pattern-table)->ArgName("realloc")->Range(0, 1);
BENCHMARK_NAMED(benchmarkImpl, switch-and-branch)->ArgName("realloc")->Range(0, 1);
BENCHMARK_NAMED(benchmarkImpl, table-2char)->ArgName("realloc")->Range(0, 1);
BENCHMARK_NAMED(benchmarkImpl, simple)->ArgName("realloc")->Range(0, 1);
BENCHMARK_NAMED(benchmarkImpl, pattern-table)->ArgName("realloc")->Range(0, 1);
BENCHMARK_NAMED(benchmarkImpl, sema)->ArgName("realloc")->Arg(1);

BENCHMARK_NAMED(benchmarkExprImpl, expr-1state)->ArgName("realloc")->Range(0, 1);
BENCHMARK_NAMED(benchmarkExprImpl, expr-2state)->ArgName("realloc")->Range(0, 1);
BENCHMARK_NAMED(benchmarkExprImpl, switch-and-branch)->ArgName("realloc")->Range(0, 1);
BENCHMARK_NAMED(benchmarkExprImpl, table-hybrid)->ArgName("realloc")->Range(0, 1);
BENCHMARK_NAMED(benchmarkExprImpl, pattern-table)->ArgName("realloc")->Range(0, 1);
// clang-format on

// `expression` only differs from the full grammar lexers in how it dispatches, so on an input
// both of them accept it has to produce exactly the same tokens, lines and comments.
TEST(Charge, ExpressionLexerMatchesSwitchAndBranch) {
    for (auto fileName : { "/../benchmark-expr/benchmark-expr.chrg",
             "/../benchmark-expr/benchmark-expr-nocomments.chrg" }) {
        padded_string source = server::readFile(std::string(COMPILER_TEST_DIR) + fileName);

        LexerOutput expected(source);
        LexerOutput actual(source);
        EXPECT_EQ(parse::lexSwitchAndBranch(source.data(), expected), source.end());
        EXPECT_EQ(parse::lexExpr2State(source.data(), actual), source.end());

        ASSERT_EQ(actual.tokens.size(), expected.tokens.size()) << fileName;
        for (int_t i = 0; i < expected.tokens.size(); i++) {
            const auto& e = expected.tokens[i];
            const auto& a = actual.tokens[i];
            ASSERT_EQ(parse::nameString(a.kind()), parse::nameString(e.kind()))
                << fileName << ":" << e.m_fields.lineNumber() << ":" << e.m_fields.column();
            ASSERT_EQ(a.m_fields.lineNumber(), e.m_fields.lineNumber()) << fileName << " token " << i;
            ASSERT_EQ(a.m_fields.column(), e.m_fields.column()) << fileName << " token " << i;
        }

        ASSERT_EQ(actual.lines.size(), expected.lines.size()) << fileName;
        ASSERT_EQ(actual.whitespace.size(), expected.whitespace.size()) << fileName;
        for (int_t i = 0; i < expected.whitespace.size(); i++) {
            ASSERT_EQ(actual.whitespace[i].tag(), expected.whitespace[i].tag()) << fileName;
            ASSERT_EQ(actual.whitespace[i].lineNumber(), expected.whitespace[i].lineNumber()) << fileName;
            ASSERT_EQ(actual.whitespace[i].column(), expected.whitespace[i].column()) << fileName;
            ASSERT_EQ(actual.whitespace[i].length, expected.whitespace[i].length) << fileName;
        }
    }
}

int charge_main(int argc, char** argv) {
    CLI::App app("Charge tool");
    argv = app.ensure_utf8(argv);
    testing::InitGoogleTest(&argc, argv);

    auto& server = *app.add_subcommand("server", "LSP language server");
    auto& serverReplay = *server.add_subcommand("replay", "Replay a file. Recording can be enabled by setting CHARGE_LSP_RECORD=<dir>");
    bool replayInRealtime = false;
    serverReplay.add_flag("--realtime", replayInRealtime, "");
    std::string replayFile;
    serverReplay.add_option("file", replayFile)->required()->check(CLI::ReadPermissions);

    auto& syntax_check = *app.add_subcommand("syntax-check", "Check the syntax of a charge file");
    std::string syntaxCheckFile;
    syntax_check.add_option("file", syntaxCheckFile)->required()->check(CLI::ReadPermissions);

    auto& dump_tokens = *app.add_subcommand("dump-tokens", "Lex a charge file and print the kind of each token, one per line");
    std::string dumpTokensFile;
    dump_tokens.add_option("file", dumpTokensFile)->required()->check(CLI::ReadPermissions);

    auto& benchmark = *app.add_subcommand("benchmark", "Benchmark a parser or lexer implementation");
    std::string benchmarkImpl;
    benchmark.add_option("impl", benchmarkImpl, "Implementation to benchmark")
        ->required()
        ->check(CLI::IsMember({ "sema", "simple", "no-output", "baseline",
            "switch-and-branch", "switch-and-pattern-table", "pattern-table",
            "table-2char", "table-hybrid", "switch-and-table", "expr-1state", "expr-2state" }));
    std::string benchmarkFile;
    benchmark.add_option("file", benchmarkFile)->required()->check(CLI::ReadPermissions);
    int benchmarkRepeats = 1;
    benchmark.add_option("-r,--repeats", benchmarkRepeats, "Number of times to repeat the benchmark")
        ->default_val(1)
        ->check(CLI::PositiveNumber);
    std::string benchmarkClearMethod = "realloc";
    benchmark.add_option("-c,--clear-method", benchmarkClearMethod, "How to clear the output between repeats")
        ->default_val("realloc")
        ->check(CLI::IsMember({ "reset", "realloc" }));

    auto& gbench = *app.add_subcommand("gbench", "Invoke google benchmark")->allow_extras(CLI::ExtrasMode::Ignore);

    CLI11_PARSE(app, argc, argv);

    if (benchmark.parsed()) {
        ClearMethod clearMethod = benchmarkClearMethod == "reset" ? ClearMethod::Reset : ClearMethod::Realloc;
        return runBenchmark(benchmarkImpl, benchmarkFile, benchmarkRepeats, clearMethod);
    }

    if (gbench.parsed()) {
        benchmark::Initialize(&argc, argv);
        benchmark::RunSpecifiedBenchmarks();
        benchmark::Shutdown();
        return 0;
    }

    if (syntax_check.parsed()) {
        auto sourceBuffer = server::readFile(syntaxCheckFile);
        sema::Context context({}, sourceBuffer);
        auto errors = parse::parseAndRecover(context);
        if (errors.empty())
            return 0;

        for (const auto& error : errors) {
            auto range = error.errorRange();
            SourceLocation startLoc = context.tokenBuffer.findSourceLocation(range.begin());
            SourceLocation endLoc = context.tokenBuffer.findSourceLocation(range.end());

            const char* lineBegin = context.tokenBuffer.lines[startLoc.lineIndex()].begin;
            const char* lineEnd = lineBegin;
            while (lineEnd[0] != '\r' && lineEnd[0] != '\n' && lineEnd[0] != '\0')
                lineEnd += 1;
            int_t highlightLength = range.length();

            if (startLoc.lineIndex() == endLoc.lineIndex()) {
                dbgln("Syntax error on line {}:", startLoc.lineNumber());
            } else {
                dbgln("Syntax error starting on line {}:", startLoc.lineNumber());
                highlightLength = lineEnd - range.begin() + 1;
            }
            dbgln("    {}", std::string_view(lineBegin, lineEnd));
            int_t offset = range.begin() - lineBegin;
            VERIFY(offset >= 0);
            dbgln("    {: >{}}{:^>{}}", "", offset, "", highlightLength);
            dbgln("");
        }
        return 1;
    }

    if (dump_tokens.parsed()) {
        padded_string source = server::readFile(dumpTokensFile);
        LexerOutput output(source);
        VERIFY(parse::lexSwitchAndBranch(source.data(), output) == source.end());
        for (const auto& token : output.tokens)
            std::cout << parse::nameString(token.kind()) << "\n";
        return 0;
    }

    if (server.parsed()) {
        if (serverReplay.parsed()) {
            server::replayLog(replayFile, replayInRealtime);
            return 0;
        }

#ifdef WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif

        server::Server s;
        s.m_messageLog = server::MessageLog::createFromEnvironment();
        while (!s.shouldExit()) {
            auto val = std::cin.get();
            if (std::cin.fail()) {
                dbgln("Reading stdin failed");
                break;
            }
            s.receiverChacacter(val);
            if (!s.outputBuffer.empty()) {
                std::cout.write(s.outputBuffer.data(), s.outputBuffer.size());
                s.outputBuffer.clear();
            }
        }
        return 0;
    }

    return RUN_ALL_TESTS();
}

int chiral_main(int argc, char** argv) {
    CLI::App app("Chiral tool");
    argv = app.ensure_utf8(argv);

    CLI11_PARSE(app, argc, argv);
    dbgln("Currently does nothing");
    return 0;
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    auto filename = fs::path(argv[0]).filename().string();
    if (filename.starts_with("charge")) {
        return charge_main(argc, argv);
    } else if (filename.starts_with("chiral")) {
        return chiral_main(argc, argv);
    } else {
        dbgln("Unsupported executable name \"{}\"", filename);
    }
}