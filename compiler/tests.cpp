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
static bool isBulkIdentifierChar(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_';
}
static uint64_t parseInteger(std::string_view str) {
    auto characterValue = [](char c) -> std::optional<int> {
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= '0' && c <= '9')
            return c - '0';

        if (c == '\'')
            return {};

        VERIFY_NOT_REACHED();
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
        VERIFY((uint64_t)curDig.value() < base);
        value = value * base + (uint64_t)curDig.value();
    }
    return value;
}

struct SemaExpr {
    virtual ~SemaExpr() = default;

    virtual void check(glue::Context& ctx, sema::Program* prog, sema::Value value) const = 0;
};
struct ParameterizeExpr : SemaExpr {
    glue::DeclarationNode* base;
    std::vector<std::unique_ptr<SemaExpr>> arguments;

    ParameterizeExpr(glue::DeclarationNode* base, std::vector<std::unique_ptr<SemaExpr>> args)
        : base(base), arguments(std::move(args)) { }

    void check(glue::Context& ctx, sema::Program* prog, sema::Value value) const override {
        VERIFY(value.kind() == sema::ValueKind::Constant);
        const auto& c = prog->constants[value.id()];
        VERIFY(c.op == sema::Program::Opcode::Parameterize);

        VERIFY(c.u.parameterize.base == base->program().value());

        VERIFY(c.u.parameterize.argumentCount == arguments.size());
        for (int_t i = 0; i < (int_t)arguments.size(); i++)
            arguments[i]->check(ctx, prog, prog->parameterizeArguments[c.u.parameterize.firstArgumentIndex + i]);
    }
};
struct LiteralExpr : SemaExpr {
    glue::DeclarationNode* literal;

    explicit LiteralExpr(glue::DeclarationNode* literal)
        : literal(literal) { }

    void check(glue::Context&, sema::Program*, sema::Value value) const override {
        VERIFY(value.kind() == sema::ValueKind::Program);
        VERIFY(literal->program().value() == value.program());
    }
};
struct ParameterExpr : SemaExpr {
    int_t parameterIndex = 0;
    explicit ParameterExpr(int_t index)
        : parameterIndex(index) { }

    void check(glue::Context&, sema::Program* prog, sema::Value value) const override {
        VERIFY(value.kind() == sema::ValueKind::Constant);
        const auto& c = prog->constants[value.id()];
        VERIFY(c.op == sema::Program::Opcode::Parameter);
        VERIFY((int_t)c.u.parameterIndex == parameterIndex);
    }
};
struct SignatureLiteralExpr : SemaExpr {
    glue::DeclarationNode* literal;

    explicit SignatureLiteralExpr(glue::DeclarationNode* literal)
        : literal(literal) { }

    void check(glue::Context&, sema::Program* prog, sema::Value value) const override {
        VERIFY(value.kind() == sema::ValueKind::Constant);
        const auto& c = prog->constants[value.id()];
        VERIFY(c.op == sema::Program::Opcode::SignatureOf);
        VERIFY(c.u.signatureProgram == literal->program().value());
    }
};

struct SemaExprParser {
    glue::Context& context;
    std::string_view& buffer;

    void skipWhitespace() {
        while (!buffer.empty() && buffer.front() == ' ')
            buffer = buffer.substr(1);
    }

    void consume(std::string_view str) {
        VERIFY(buffer.starts_with(str));
        buffer = buffer.substr(str.length());
        skipWhitespace();
    }

    std::string_view readId() {
        std::string_view copy = buffer;
        while (!buffer.empty() && isBulkIdentifierChar(buffer.front()))
            buffer = buffer.substr(1);
        auto result = copy.substr(0, copy.size() - buffer.size());
        VERIFY(!result.empty());
        skipWhitespace();
        return result;
    }

    glue::DeclarationNode* readNestedName() {
        glue::DeclarationNode* node = context.currentScope();

        for (;;) {
            auto id = readId();
            node = node->findChild(context.wordTable.get(id));
            VERIFY(node != nullptr);

            if (!buffer.starts_with("::"))
                break;
            consume("::");
        }

        return node;
    }

    auto parseArguments() {
        std::vector<std::unique_ptr<SemaExpr>> args;
        for (;;) {
            args.push_back(parse());
            if (buffer.starts_with("}"))
                break;
            consume(",");
        }
        return args;
    }

    std::unique_ptr<SemaExpr> parse() {
        if (buffer.starts_with("#")) {
            buffer = buffer.substr(1);
            auto id = readId();
            return std::make_unique<ParameterExpr>(parseInteger(id));
        }
        if (buffer.starts_with("@")) {
            buffer = buffer.substr(1);
            auto id = readId();
            if (id == "sigof") {
                consume("(");
                glue::DeclarationNode* base = readNestedName();
                consume(")");
                return std::make_unique<SignatureLiteralExpr>(base);
            }
        }

        glue::DeclarationNode* node = readNestedName();

        if (buffer.starts_with("{")) {
            consume("{");
            auto args = parseArguments();
            consume("}");
            return std::make_unique<ParameterizeExpr>(node, std::move(args));
        }
        return std::make_unique<LiteralExpr>(node);
    }
};

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
        "expect-no-error", "expect-token", "expect-source-position",
        "line", "column", "packed-range-begin-column", "expect-identifier", "name",
        "expect-type", "expect-value");

    WordStringTable wordTable { words };
    glue::Context context;
    CommandQueue commandQueue;

    TestInstrumenter(std::string_view source)
        : context { source } { sema::Generator::generateBuiltins(context); }

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
        if (commandQueue.top().command == words["expect-token"]) {
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
            while (comment.length() > 0 && isBulkCommandChar(comment.front())) {
                comment = comment.substr(1);
            }
            cmdStr = cmdStr.substr(0, cmdStr.length() - comment.length());
            skipWhitespace();

            auto word = wordTable.get(cmdStr);
            if (word == words["expect-type"] || word == words["expect-value"]) {
                handleSemanticCommand(word, whitespace, comment);
                return;
            }
            Command command { word };
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
    void handleSemanticCommand(Word word, parse::WhitespaceInfo whitespace, std::string_view& comment) {
        SemaExprParser parser { context, comment };
        auto expr = parser.parse();
        sema::Program* program = nullptr;
        for (auto& node : context.storage) {
            if (!node.parseLocation().has_value())
                continue;
            auto tokenInfo = context.parseOutput.tokens[node.parseLocation()->id()];
            if (tokenInfo > whitespace) {
                auto progHandle = sema::Generator::signatureCheck(context, &node);
                program = &context.programs[progHandle.id()];
                break;
            }
        }
        VERIFY(program != nullptr);
        program->dump(context);

        if (word == words["expect-type"])
            expr->check(context, program, (sema::Value)program->type());
        if (word == words["expect-value"])
            expr->check(context, program, (sema::Value)program->value());
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