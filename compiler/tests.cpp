#include "compiler.h"
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

struct TestInstrumenter : Parser::Instrumenter, Parser::ErrorHandler {
    enum class TestMode {
        Invalid,
        Lexer,
        Parser,
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
        "line", "column", "packed-range-begin-column", "expect-decl", "expect-identifier", "name");
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

    void runTest(std::string_view source) {
        Parser par;
        par.setSource(source);
        par.errorHandler = this;
        par.instrumenter = this;
        par.nextToken();

        while (par.tok != Token::EOS) {
            switch (testMode) {
            case TestMode::Lexer:
                par.nextToken();
                break;
            case TestMode::Parser: {
                std::cout << "------\n";
                par.parseDeclaration();
                dump((Node*)(par.nodeStream.storage + par.staticDeclStack.back().nodeStreamOffset), par.wordTable);
                break;
            }
            default:
                error();
            }
        }
        verify(commandQueue.empty());
    }

    void nextToken(Parser* par) override {
        // fmt::println("tok {}", nameString(par->tok));
        verify(((LexerState*)par)->valid());
        if (commandQueue.empty())
            return;
        if (commandQueue.top().command == words["expect-token"]) {
            auto cmd = commandQueue.pop();
            expect_eq(cmd.pairs.size(), (size_t)1, "", &cmd);
            expect_eq(cmd.pairs[0].key, Word(), "", &cmd, &cmd.pairs[0]);
            expect_eq(cmd.pairs[0].value, nameString(par->tok), "", &cmd, &cmd.pairs[0]);
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
        // std::cout << "emitting decl " << decl << " - " << nameString(decl->kind()) << '\n';
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

    void handleComment(Parser* par) override {
        VERIFY(isComment(par->tok));
        std::string_view comment = par->tokCommentSource();
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
            case words["expect-source-position"].asUint(): {
                SourcePosition pos = par->sourcePosition(par->tokRange().first());
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
        Command cmd = commandQueue.pop();
        if (cmd.command != cause) {
            std::cout << "pending command '" << wordTable.view(cmd.command)
                      << "' but got '" << wordTable.view(cause) << "'\n";
            VERIFY_NOT_REACHED();
        }
        return cmd;
    }
    LexerAction invalidCharacter(Parser* par, char) override {
        auto cmd = popCommand(words["expect-invalid-char"]);
        verifyNoPairs(cmd);
        // skip over character
        par->sourceOffset += 1;
        return LexerAction::Retry;
    }
    LexerAction unterminatedBlockComment(Parser* par, int_t) override {
        auto cmd = popCommand(words["expect-unterm-comment"]);
        verifyNoPairs(cmd);
        // emit EOS token
        par->tok = Token::EOS;
        par->tokData = std::nullopt;
        return LexerAction::AcceptState;
    }
    LexerAction invalidCharacterLiteral(Parser* par, int_t, int_t endOffset) override {
        auto cmd = popCommand(words["expect-invalid-char-literal"]);
        verifyNoPairs(cmd);
        // skip over literal
        par->sourceOffset = endOffset + 1;
        return LexerAction::Retry;
    }
    LexerAction unterminatedCharacterLiteral(Parser* par, int_t, int_t endOffset) override {
        auto cmd = popCommand(words["expect-unterm-char-literal"]);
        verifyNoPairs(cmd);
        // skip over remaining line
        par->sourceOffset = endOffset;
        return LexerAction::Retry;
    }
};

struct BadErrorHandler : Parser::ErrorHandler {
    LexerAction invalidCharacter(Parser*, char) override {
        VERIFY_NOT_REACHED();
    }
    LexerAction unterminatedBlockComment(Parser*, int_t) override {
        VERIFY_NOT_REACHED();
    }
    LexerAction invalidCharacterLiteral(Parser*, int_t, int_t) override {
        VERIFY_NOT_REACHED();
    }
    LexerAction unterminatedCharacterLiteral(Parser*, int_t, int_t) override {
        VERIFY_NOT_REACHED();
    }
};

int main() {
    std::filesystem::path testDir { COMPILER_TEST_DIR };
    for (const auto& entry : std::filesystem::directory_iterator(testDir)) {
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
        test.runTest({ sourceBuffer.get(), (size_t)length });
    }

    BadErrorHandler errorHandler;
    Parser par;
    auto printToken = [](Token token) {
        std::cout << toSmallString(token);
        std::cout << ' ';
    };
    par.errorHandler = &errorHandler;
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