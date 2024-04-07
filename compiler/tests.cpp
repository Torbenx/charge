#include <WordTable.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <log.h>
#include <parse/parse_impl.h>
#include <sema/Generator.h>
#include <vector>

static bool isBulkCommandChar(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '-';
}
static bool isFirstCommandChar(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static bool isCommandEndChar(uint8_t c) {
    return c == '\r' || c == '\n' || c == ';';
}

struct TestInstrumenter : parse::OutputVisitor<TestInstrumenter>, parse::ErrorHandler {
    struct Pair {
        Word key;
        std::string_view value;
    };
    struct Command {
        Word command;
        std::vector<Pair> pairs = {};
    };
    struct CommandQueue {
        std::list<Command> queue;
        Command pop() {
            VERIFY(queue.size() > 0);
            Command cmd = std::move(queue.front());
            queue.pop_front();
            return cmd;
        }
        void push(Command cmd) {
            queue.emplace_back(std::move(cmd));
        }
        const Command& top() const {
            VERIFY(queue.size() > 0);
            return queue.front();
        }
        bool empty() const { return queue.empty(); }
    };

    static constexpr auto words = ConstWordStringTable(
        "expect-invalid-char", "expect-unterm-comment", "expect-unterm-char-literal", "expect-invalid-char-literal",
        "expect-no-error", "expect-token", "expect-node", "parser-test", "lexer-test", "expect-source-position",
        "line", "column", "packed-range-begin-column", "expect-decl", "expect-identifier", "name", "semantic-test",
        "benchmark", "expect-expected-parameter-name", "expect-parameter-modifier-not-allowed",
        "expect-invalid-parameter-modifier", "expect-unexpected-after-parameter", "expect-expected-semicolon",
        "expect-expected-function-body", "expect-expected-if-body", "expect-expected-else-body");

    WordStringTable wordTable { words };
    glue::Context context;
    CommandQueue commandQueue;

    TestInstrumenter(std::string_view source)
        : context { source } { }

    [[noreturn]] void error(std::string_view = {}, const Command* = nullptr, const Pair* = nullptr) {
        VERIFY_NOT_REACHED();
    }
    void verify(bool b, std::string_view = {}, const Command* = nullptr, const Pair* = nullptr) {
        VERIFY(b);
    }
    void verifyNoPairs(const Command& cmd) {
        verify(cmd.pairs.size() == 0, "", &cmd);
    }
    template<typename T>
    void expect_eq(const T& left, const T& right, std::string_view = {}, const Command* = nullptr, const Pair* = nullptr) {
        VERIFY(left == right);
    }
    [[noreturn]] void invalidKey(const Command*, const Pair*) {
        VERIFY_NOT_REACHED();
    }

    void visitWhitespace(parse::WhitespaceInfo info) {
        if (info.tag() == parse::WhitespaceKind::LineComment) {
            handleComment(info, context.parseOutput.whitespaceSpelling(info).substr(2));
        }
        if (info.tag() == parse::WhitespaceKind::BlockComment) {
            auto spelling = context.parseOutput.whitespaceSpelling(info);
            handleComment(info, spelling.substr(2, spelling.length() - 4));
        }
    }

    void visitToken(parse::TokenInfo tok) {
        // std::cout << nameString(tok.kind()) << '\n';

        if (commandQueue.empty())
            return;
        if (commandQueue.top().command == words["expect-node"]) {
            auto cmd = commandQueue.pop();
            for (const auto& pair : cmd.pairs) {
                if (pair.key == Word())
                    expect_eq(pair.value, nameString(tok.kind()), "", &cmd, &pair);
                // else if (pair.key == words["packed-range-begin-column"])
                //     expect_eq<uint32_t>(par->sourcePosition(node->packedToken().first()).column, parseInteger(pair.value), "", &cmd, &pair);
                else
                    invalidKey(&cmd, &pair);
            }
        } else if (commandQueue.top().command == words["expect-identifier"]) {
            auto cmd = commandQueue.pop();
            expect_eq(tok.kind(), parse::TokenKind::IdentifierExpr);
            for (const auto& pair : cmd.pairs) {
                if (pair.key == Word())
                    expect_eq(pair.value, context.wordTable.view(Word::fromUint(tok.data())), "", &cmd, &pair);
                else
                    invalidKey(&cmd, &pair);
            }
        }
    }

    void runTest() {
        parse::parseImpl(context.parseOutput.source.data(), context, this);
        VERIFY(context.currentScope()->declaringNode() == nullptr);

        visit(context.parseOutput);
    }

    void handleComment(parse::WhitespaceInfo whitespace, std::string_view comment) {
        auto skipWhitespace = [&]() {
            while (comment.length() > 0 && (comment.front() == ' ' || comment.front() == '\t'))
                comment = comment.substr(1);
        };
        skipWhitespace();

        while (comment.length() > 0) {
            if (comment.front() == ';')
                break;
            std::string_view cmdStr = comment;
            Word::HashState hashState;
            while (comment.length() > 0 && isBulkCommandChar(comment.front())) {
                Word::iterateHash(hashState, comment.front());
                comment = comment.substr(1);
            }
            auto hash = Word::finalizeHash(hashState);
            cmdStr = cmdStr.substr(0, cmdStr.length() - comment.length());
            skipWhitespace();

            Command command { wordTable.getWithHash(cmdStr, hash) };
            while (comment.length() > 0 && !isCommandEndChar(comment.front())) {
                std::string_view savedComment = comment;
                Word key = {};
                if (isFirstCommandChar(comment.front())) {
                    std::string_view keyStr = comment;
                    do {
                        comment = comment.substr(1);
                    } while (comment.length() > 0 && isBulkCommandChar(comment.front()));
                    keyStr = keyStr.substr(0, keyStr.length() - comment.length());
                    skipWhitespace();

                    if (!comment.empty() && comment.front() == '=') {
                        comment = comment.substr(1);
                        key = wordTable.get(keyStr);
                        skipWhitespace();
                    } else {
                        // not a key-value pair, restore
                        comment = savedComment;
                    }
                }
                std::string_view valueStr = comment;
                while (comment.length() > 0 && comment.front() != ' ' && comment.front() != '\t'
                    && !isCommandEndChar(comment.front())) {
                    comment = comment.substr(1);
                }
                valueStr = valueStr.substr(0, valueStr.length() - comment.length());
                command.pairs.push_back({ key, valueStr });
                skipWhitespace();
            }

            switch (command.command.asUint()) {
            case words["expect-no-error"].asUint(): {
                verifyNoPairs(command);
                verify(commandQueue.empty(), "", &command);
                break;
            }
            case words["expect-source-position"].asUint(): {
                for (const auto& pair : command.pairs) {
                    if (pair.key == words["line"])
                        expect_eq<uint32_t>(whitespace.lineNumber(), parseInteger(pair.value), "", &command, &pair);
                    else if (pair.key == words["column"])
                        expect_eq<uint32_t>(whitespace.column(), parseInteger(pair.value), "", &command, &pair);
                    else
                        invalidKey(&command, &pair);
                }
                break;
            }
            default:
                commandQueue.push(std::move(command));
                break;
            }

            if (comment.length() > 0 && isCommandEndChar(comment.front())) {
                comment = comment.substr(1);
                skipWhitespace();
            }
        }
    }
    uint64_t parseInteger(std::string_view str) {
        auto characterValue = [this](char c) -> std::optional<int> {
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            if (c >= '0' && c <= '9')
                return c - '0';

            if (c == '\'')
                return {};

            error();
        };

        uint64_t base = 10;
        if (str.front() == '0' && str.length() > 1) {
            char baseChar = str[1];
            if (baseChar == 'x' || baseChar == 'X') {
                base = 16;
                str = str.substr(2);
            } else if (baseChar == 'b' || baseChar == 'B') {
                base = 2;
                str = str.substr(2);
            }
        }
        uint64_t value = 0;
        while (str.length() > 0) {
            auto curDig = characterValue(str.front());
            str = str.substr(1);
            if (!curDig.has_value())
                continue;
            verify((uint64_t)curDig.value() < base);
            value = value * base + (uint64_t)curDig.value();
        }
        return value;
    }

    Command popCommand(Word cause) {
        if (commandQueue.empty()) {
            fmt::println("got error '{}' without pending command", wordTable.view(cause));
            VERIFY_NOT_REACHED();
        }
        Command cmd = commandQueue.pop();
        if (cmd.command != cause) {
            fmt::println("got error '{}' but pending command is '{}'", wordTable.view(cause), wordTable.view(cmd.command));
            VERIFY_NOT_REACHED();
        }
        return cmd;
    }

    void invalidToken(parse::LexerToken token, parse::State state, parse::ScopeKind* scopes, glue::Context& context) override {
        fmt::println("");
        fmt::println(
            "Invalid token '{}' for state '{}' and scope '{}' on line {}",
            parse::nameString(token), parse::nameString(state), parse::nameString(scopes[0]), context.parseOutput.lines.size());
        fmt::println("scopes:");
        for (;;) {
            fmt::println("  {}", parse::nameString(*scopes));
            if (*scopes == parse::ScopeKind::Invalid)
                break;
            scopes -= 1;
        }
        fmt::println("");
        VERIFY_NOT_REACHED();
    }
};

int main() {
    {
        TestInstrumenter inst(R"str(
            struct X: { }
            static XX: type = X;
            template(x: X) struct Container: {
                template(x: XX) struct Y: { }
            }
            static Z = Container;
        )str");
        inst.runTest();
        auto& ctx = inst.context;
        auto checkAndDump = [&ctx](std::string name) {
            glue::DeclarationNode* node = ctx.currentScope();
            for (;;) {
                size_t offset = name.find("::");
                if (offset == std::string::npos)
                    offset = name.length();
                node = node->findChild(ctx.wordTable.get(name.substr(0, offset))).value();
                if (offset == name.length())
                    break;
                name = name.substr(offset + 2);
            }
            auto* prog = sema::Generator::signatureCheck(node);
            fmt::println("------------------------------------");
            prog->dump();
        };
        checkAndDump("XX");
        checkAndDump("Container::Y");
        checkAndDump("Z");
    }

    namespace fs = std::filesystem;
    fs::path testDir { COMPILER_TEST_DIR };
    for (const auto& entry : fs::directory_iterator(testDir)) {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension().string() != ".chrg")
            continue;

        std::ifstream stream;
        stream.open(entry.path(), std::ios::binary);
        VERIFY(stream.good());
        stream.seekg(0, std::ios::end);
        int_t length = stream.tellg();
        VERIFY(length >= 0);
        auto sourceBuffer = std::make_unique<char[]>(length + 2);
        stream.seekg(0, std::ios::beg);
        stream.read(sourceBuffer.get(), length);
        stream.close();
        VERIFY(stream.good());
        sourceBuffer[length] = '\0';
        sourceBuffer[length + 1] = '\0';

        TestInstrumenter test;
        test.runTest(fs::relative(fs::canonical(entry.path()), testDir), { sourceBuffer.get(), (size_t)length });
    }
}