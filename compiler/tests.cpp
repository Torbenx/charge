#include "Parser.h"
#include "semantic.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
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

struct TestInstrumenter : Lexer::Instrumenter, Lexer::ErrorHandler, Parser::Instrumenter, Parser::ErrorHandler {
    enum class TestMode {
        Invalid,
        Lexer,
        Parser,
        Semantic,
        Benchmark,
    };
    using Value = std::variant<std::nullopt_t, NumericLiteral, CharacterLiteral>;
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
    TestMode testMode = TestMode::Invalid;
    CommandQueue commandQueue;

    static constexpr auto words = ConstWordStringTable(
        "expect-invalid-char", "expect-unterm-comment", "expect-unterm-char-literal", "expect-invalid-char-literal",
        "expect-no-error", "expect-token", "expect-node", "parser-test", "lexer-test", "expect-source-position",
        "line", "column", "packed-range-begin-column", "expect-decl", "expect-identifier", "name", "semantic-test",
        "benchmark", "expect-expected-parameter-name", "expect-parameter-modifier-not-allowed",
        "expect-invalid-parameter-modifier", "expect-unexpected-after-parameter", "expect-expected-semicolon",
        "expect-expected-function-body", "expect-expected-if-body", "expect-expected-else-body");
    WordStringTable wordTable { words };

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

    void runTest(std::filesystem::path file, std::string_view source) {
        Parser par;
        try {
            par.Lexer::errorHandler = this;
            par.Lexer::instrumenter = this;
            par.Parser::errorHandler = this;
            par.Parser::instrumenter = this;
            par.setSource(source);
            par.nextToken();

            while (par.tok != Token::EOS) {
                switch (testMode) {
                case TestMode::Lexer: {
                    par.nextToken();
                    break;
                }
                case TestMode::Parser: {
                    std::cout << "------\n";
                    [[maybe_unused]] NamespaceDecl* global = par.parseModule();
                    // dumpSyntaxTree(global, par.wordTable);
                    break;
                }
                case TestMode::Semantic: {
                    std::cout << "------\n";
                    NamespaceDecl* global = par.parseModule();
                    // dump(global, par.wordTable);
                    SemanticContext sema;
                    sema.wordTable = &par.wordTable;
                    sema.check(global->staticDecls());
                    break;
                }
                case TestMode::Benchmark: {
                    std::cout << "------\n";
                    using Clock = std::chrono::high_resolution_clock;
                    {
                        // lexer test
                        Parser par2;
                        par2.sourceBuffer = par.sourceBuffer;
                        auto start = Clock::now();
                        do
                            par2.nextToken();
                        while (par2.tok != Token::EOS);
                        auto stop = Clock::now();
                        std::cout << "Lexing file took " << std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(stop - start);
                        std::cout << " and produced " << par2.tokenStream.offset.bytes() / 100'000 / 10.0 << "MB of tokens.\n";
                    }
                    {
                        // parse test
                        Parser par2;
                        par2.sourceBuffer = par.sourceBuffer;
                        auto start = Clock::now();
                        par2.nextToken();
                        par2.parseModule();
                        auto stop = Clock::now();
                        std::cout << "Parsing file took " << std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(stop - start);
                        std::cout << " and produced " << par2.nodeStream.offset.bytes() / 100'000 / 10.0 << "MB of nodes";
                        std::cout << " and " << par2.tokenStream.offset.bytes() / 100'000 / 10.0 << "MB of tokens.\n";
                    }
                    return;
                }
                default:
                    error();
                }
            }
            verify(commandQueue.empty());
        } catch (const std::exception& e) {
            std::cout << "test " << file << " failed: what() = \"" << e.what() << "\"\n";
            SourcePosition pos = par.sourcePosition(par.tokRange().first());
            std::cout << "parser at " << file.string() << ":" << pos.line << ":" << pos.column << ", tok=" << nameString(par.tok) << "\n";
            par.formatLine(std::cout, par.tokRange());
        }
    }

    void nextToken(Lexer* lex) override {
        // fmt::println("tok {}", nameString(lex->tok));
        verify(lex->valid());
        if (commandQueue.empty())
            return;
        if (commandQueue.top().command == words["expect-token"]) {
            auto cmd = commandQueue.pop();
            expect_eq(cmd.pairs.size(), (size_t)1, "", &cmd);
            expect_eq(cmd.pairs[0].key, Word(), "", &cmd, &cmd.pairs[0]);
            expect_eq(cmd.pairs[0].value, nameString(lex->tok), "", &cmd, &cmd.pairs[0]);
        }
    }

    void emitNode(Parser* par, Node* node) override {
        // std::cout << "emitting at " << node << " - " << nameString(node->kind()) << '\n';
        if (commandQueue.empty())
            return;
        if (commandQueue.top().command == words["expect-node"]) {
            auto cmd = commandQueue.pop();
            for (const auto& pair : cmd.pairs) {
                if (pair.key == Word())
                    expect_eq(pair.value, nameString(node->kind()), "", &cmd, &pair);
                else if (pair.key == words["packed-range-begin-column"])
                    expect_eq<uint32_t>(par->sourcePosition(node->packedToken().first()).column, parseInteger(pair.value), "", &cmd, &pair);
                else
                    invalidKey(&cmd, &pair);
            }
        } else if (commandQueue.top().command == words["expect-identifier"]) {
            auto cmd = commandQueue.pop();
            expect_eq(node->kind(), NodeKind::IdentifierExpr);
            for (const auto& pair : cmd.pairs) {
                if (pair.key == Word())
                    expect_eq(pair.value, par->wordTable.view(((IdentifierExpr*)node)->id), "", &cmd, &pair);
                else
                    invalidKey(&cmd, &pair);
            }
        }
    }
    void emitDecl(Parser* par, Decl* decl) override {
        // std::cout << "emitting decl '" << par->wordTable.view(decl->name) << "' - " << nameString(decl->kind()) << '\n';
        if (commandQueue.empty())
            return;
        if (commandQueue.top().command == words["expect-decl"]) {
            auto cmd = commandQueue.pop();
            for (const auto& pair : cmd.pairs) {
                if (pair.key == Word())
                    expect_eq(pair.value, nameString(decl->kind()), "", &cmd, &pair);
                else if (pair.key == words["name"])
                    expect_eq(pair.value, par->wordTable.view(decl->name));
                else
                    invalidKey(&cmd, &pair);
            }
        }
    }

    void handleComment(Lexer* lex) override {
        VERIFY(isComment(lex->tok));
        std::string_view comment = lex->tokCommentSource();
        auto skipWhitespace = [&]() {
            while (comment.length() > 0 && (comment.front() == ' ' || comment.front() == '\t'))
                comment = comment.substr(1);
        };
        skipWhitespace();

        while (comment.length() > 0) {
            if (comment.front() == ';')
                break;
            std::string_view cmdStr = comment;
            uint32_t hash = 0;
            while (comment.length() > 0 && isBulkCommandChar(comment.front())) {
                hash = Word::iterateHash(hash, comment.front());
                comment = comment.substr(1);
            }
            hash = Word::finalizeHash(hash);
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

                    if (comment.front() == '=') {
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
            case words["lexer-test"].asUint(): {
                verifyNoPairs(command);
                testMode = TestMode::Lexer;
                break;
            }
            case words["parser-test"].asUint(): {
                verifyNoPairs(command);
                testMode = TestMode::Parser;
                break;
            }
            case words["semantic-test"].asUint(): {
                verifyNoPairs(command);
                testMode = TestMode::Semantic;
                break;
            }
            case words["benchmark"].asUint(): {
                verifyNoPairs(command);
                testMode = TestMode::Benchmark;
                break;
            }
            case words["expect-source-position"].asUint(): {
                SourcePosition pos = lex->sourcePosition(lex->tokRange().first());
                for (const auto& pair : command.pairs) {
                    if (pair.key == words["line"])
                        expect_eq<uint32_t>(pos.line, parseInteger(pair.value), "", &command, &pair);
                    else if (pair.key == words["column"])
                        expect_eq<uint32_t>(pos.column, parseInteger(pair.value), "", &command, &pair);
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
    // lexer errors
    LexerAction invalidCharacter(Lexer* lex, char) override {
        auto cmd = popCommand(words["expect-invalid-char"]);
        verifyNoPairs(cmd);
        // skip over character
        lex->sourceOffset += 1;
        return LexerAction::Retry;
    }
    LexerAction unterminatedBlockComment(Lexer* lex, int_t) override {
        auto cmd = popCommand(words["expect-unterm-comment"]);
        verifyNoPairs(cmd);
        // emit EOS token
        lex->tok = Token::EOS;
        lex->tokData = std::nullopt;
        return LexerAction::AcceptState;
    }
    LexerAction invalidCharacterLiteral(Lexer* lex, int_t, int_t endOffset) override {
        auto cmd = popCommand(words["expect-invalid-char-literal"]);
        verifyNoPairs(cmd);
        // skip over literal
        lex->sourceOffset = endOffset + 1;
        return LexerAction::Retry;
    }
    LexerAction unterminatedCharacterLiteral(Lexer* lex, int_t, int_t endOffset) override {
        auto cmd = popCommand(words["expect-unterm-char-literal"]);
        verifyNoPairs(cmd);
        // skip over remaining line
        lex->sourceOffset = endOffset;
        return LexerAction::Retry;
    }

    // parser errors
    void expectedParameterName(Parser* par) override {
        auto cmd = popCommand(words["expect-expected-parameter-name"]);
        verifyNoPairs(cmd);
        // skip token
        par->nextToken();
    }
    void parameterModifierNotAllowed(Parser*, WordAndLocation, WordAndLocation) override {
        auto cmd = popCommand(words["expect-parameter-modifier-not-allowed"]);
        verifyNoPairs(cmd);
        // ignore modifier
    }
    void invalidParameterModifier(Parser*, WordAndLocation, WordAndLocation) override {
        auto cmd = popCommand(words["expect-invalid-parameter-modifier"]);
        verifyNoPairs(cmd);
        // ignore modifier
    }
    void unexpectedAfterParameter(Parser* par, WordAndLocation) override {
        auto cmd = popCommand(words["expect-unexpected-after-parameter"]);
        verifyNoPairs(cmd);
        // skip token
        par->nextToken();
        
    }
    void expectedSemiColon(Parser*) override {
        auto cmd = popCommand(words["expect-expected-semicolon"]);
        verifyNoPairs(cmd);
        // ignore
    }
    void expectedFunctionBody(Parser*) override {
        VERIFY_NOT_REACHED();
    }
    void expectedIfBody(Parser*, bool) override {
        VERIFY_NOT_REACHED();
    }
    void expectedElseBody(Parser*) override {
        VERIFY_NOT_REACHED();
    }
    void expectedAccessExpr(Parser*) override {
        VERIFY_NOT_REACHED();
    }
    void expectedExpression(Parser*) override {
        VERIFY_NOT_REACHED();
    }
    void unexpectedAfterArgument(Parser*) override {
        VERIFY_NOT_REACHED();
    }
};

struct BadErrorHandler : Lexer::ErrorHandler {
    LexerAction invalidCharacter(Lexer*, char) override {
        VERIFY_NOT_REACHED();
    }
    LexerAction unterminatedBlockComment(Lexer*, int_t) override {
        VERIFY_NOT_REACHED();
    }
    LexerAction invalidCharacterLiteral(Lexer*, int_t, int_t) override {
        VERIFY_NOT_REACHED();
    }
    LexerAction unterminatedCharacterLiteral(Lexer*, int_t, int_t) override {
        VERIFY_NOT_REACHED();
    }
};

int main() {
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
        auto sourceBuffer = std::make_unique<char[]>(length + 1);
        stream.seekg(0, std::ios::beg);
        stream.read(sourceBuffer.get(), length);
        stream.close();
        VERIFY(stream.good());
        sourceBuffer[length] = '\0';

        TestInstrumenter test;
        test.runTest(fs::relative(fs::canonical(entry.path()), testDir), { sourceBuffer.get(), (size_t)length });
    }

    BadErrorHandler errorHandler;
    Parser par;
    auto printToken = [](Token token) {
        std::cout << toSmallString(token);
        std::cout << ' ';
    };
    par.Lexer::errorHandler = &errorHandler;
    par.setSource("abcdef * psdkjhglasoidhgalsoiuhdpisuhbg98asegsuieg735uhalibliABL + b * q + c / z");
    while (par.tok != Token::EOS) {
        par.nextToken();
        printToken(par.tok);
    }
    std::cout << '\n';
    for (const auto& token : par.tokenStream)
        printToken(token.token());
    std::cout << '\n';
}