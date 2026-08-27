#include <parse/api.h>
#include <server/MessageLog.h>
#include <server/Server.h>

#define CLI11_ENABLE_EXTRA_VALIDATORS 1

#include <CLI/CLI.hpp>
#include <gtest/gtest.h>

#include <filesystem>

#ifdef WIN32
#include <fcntl.h>
#include <io.h>
#endif

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

    CLI11_PARSE(app, argc, argv);

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