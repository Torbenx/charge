#include <parse/Parser.h>
#include <parse/api.h>
#include <sema/Generator.h>
#include <server/MessageLog.h>
#include <server/Server.h>

#define CLI11_ENABLE_EXTRA_VALIDATORS 1

#include <CLI/CLI.hpp>
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
    PerfEnable() {
        char* ctlFdStr = std::getenv("PERF_CTL_FD");
        char* ackFdStr = std::getenv("PERF_ACK_FD");
        if (ctlFdStr != nullptr && ackFdStr != nullptr) {
            ctlFd = std::atoi(ctlFdStr);
            ackFd = std::atoi(ackFdStr);
        }
        if (ctlFd != 0 && ackFd != 0)  {
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

namespace parse {
    const char* lexSwitchAndBranch(const char* sourcePosition, LexerOutput& output);
    const char* lexTable2Char(const char* sourcePosition, LexerOutput& output);
    const char* lexSwitchAndPatternTable(const char* sourcePosition, LexerOutput& output);
    const char* lexPatternTable(const char* sourcePosition, LexerOutput& output);
    const char* lexTableHybrid(const char* sourcePosition, LexerOutput& output);
    const char* lexSwitchAndTable(const char* sourcePosition, LexerOutput& output);
}

static void reportDuration(Clock::time_point start, Clock::time_point stop) {
    std::cout << "Processing took " << std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(stop - start);
}

static int benchmarkSema(const padded_string& sourceBuffer, int repeats) {
    for (int i = 0; i < repeats; i++) {
        sema::Context context({}, sourceBuffer);
        parse::Parser parser(sourceBuffer);
        auto start = Clock::now();
        {
            PerfEnable enable;
            parser.parse(context);
        }
        auto stop = Clock::now();
        reportDuration(start, stop);
        std::cout << " and produced " << context.tokenBuffer.tokens.size() << " semantic tokens.\n";
        if (!parser.done() || context.m_scopeStack.size() != 1) {
            dbgln("Benchmark failed: the parser did not consume the entire input");
            return 1;
        }
    }
    return 0;
}

static int benchmarkSimple(const padded_string& sourceBuffer, int repeats) {
    for (int i = 0; i < repeats; i++) {
        parse::SimpleOutput output(sourceBuffer);
        parse::SimpleParser parser(sourceBuffer);
        auto start = Clock::now();
        {
            PerfEnable enable;
            parser.parse(output);
        }
        auto stop = Clock::now();
        reportDuration(start, stop);
        std::cout << " and produced " << output.tokenBuffer.tokens.size() << " semantic tokens.\n";
        if (!parser.done()) {
            dbgln("Benchmark failed: the parser did not consume the entire input");
            return 1;
        }
    }
    return 0;
}

static int benchmarkNoOutput(const padded_string& sourceBuffer, int repeats) {
    for (int i = 0; i < repeats; i++) {
        parse::NoOutput output;
        parse::SimpleParser parser(sourceBuffer);
        auto start = Clock::now();
        {
            PerfEnable enable;
            parser.parse(output);
        }
        auto stop = Clock::now();
        reportDuration(start, stop);
        std::cout << " and visited " << parser.parsedTokens() << " tokens.\n";
        if (!parser.done()) {
            dbgln("Benchmark failed: the parser did not consume the entire input");
            return 1;
        }
    }
    return 0;
}

static int benchmarkLexer(LexerFunction lexer, const padded_string& sourceBuffer, int repeats) {
    for (int i = 0; i < repeats; i++) {
        LexerOutput output(sourceBuffer);
        auto start = Clock::now();
        const char* finalPos = nullptr;
        {
            PerfEnable enable;
            finalPos = lexer(sourceBuffer.data(), output);
        }
        auto stop = Clock::now();
        reportDuration(start, stop);
        std::cout << " and produced " << output.tokens.size() << " tokens.\n";
        if (finalPos != sourceBuffer.data() + sourceBuffer.size()) {
            dbgln("Benchmark failed: the lexer did not consume the entire input");
            return 1;
        }
    }
    return 0;
}

static int runBenchmark(const std::string& impl, const std::string& file, int repeats) {
    auto sourceBuffer = server::readFile(file);

    if (impl == "sema")
        return benchmarkSema(sourceBuffer, repeats);
    if (impl == "simple")
        return benchmarkSimple(sourceBuffer, repeats);
    if (impl == "no-output")
        return benchmarkNoOutput(sourceBuffer, repeats);

    LexerFunction lexer = nullptr;
    if (impl == "switch-and-branch")
        lexer = parse::lexSwitchAndBranch;
    else if (impl == "switch-and-pattern-table")
        lexer = parse::lexSwitchAndPatternTable;
    else if (impl == "pattern-table")
        lexer = parse::lexPatternTable;
    else if (impl == "table-2char")
        lexer = parse::lexTable2Char;
    else if (impl == "table-hybrid")
        lexer = parse::lexTableHybrid;
    else if (impl == "switch-and-table")
        lexer = parse::lexSwitchAndTable;

    VERIFY(lexer != nullptr);
    return benchmarkLexer(lexer, sourceBuffer, repeats);
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

    auto& benchmark = *app.add_subcommand("benchmark", "Benchmark a parser or lexer implementation");
    std::string benchmarkImpl;
    benchmark.add_option("impl", benchmarkImpl, "Implementation to benchmark")
        ->required()
        ->check(CLI::IsMember({ "sema", "simple", "no-output",
            "switch-and-branch", "switch-and-pattern-table", "pattern-table",
            "table-2char", "table-hybrid", "switch-and-table" }));
    std::string benchmarkFile;
    benchmark.add_option("file", benchmarkFile)->required()->check(CLI::ReadPermissions);
    int benchmarkRepeats = 1;
    benchmark.add_option("-r,--repeats", benchmarkRepeats, "Number of times to repeat the benchmark")
        ->default_val(1)
        ->check(CLI::PositiveNumber);

    CLI11_PARSE(app, argc, argv);

    if (benchmark.parsed())
        return runBenchmark(benchmarkImpl, benchmarkFile, benchmarkRepeats);

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