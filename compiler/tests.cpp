#include <WordStringTable.h>
#include <log.h>

#include <parse/parse_impl.h>
#include <sema/Generator.h>

#include <server/Server.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <ranges>
#include <vector>

#ifdef WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char** argv) {
    if (argc >= 2 && std::string_view(argv[1]) == std::string_view("--server")) {
#ifdef WIN32
            _setmode(_fileno(stdin), _O_BINARY);
            _setmode(_fileno(stdout), _O_BINARY);
#endif

        server::Server s;
        while (!s.shouldExit()) {
            auto val = std::cin.get();
            if (std::cin.fail()) {
                println("Reading stdin failed");
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

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

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

namespace sema {

struct NestedName {
    std::vector<Word> parts;

    bool match(Context& ctx, DeclarationValue value) const {
        for (Word expectedName : std::views::reverse(parts)) {
            if (value.kind() == DeclarationValueKind::Program) {
                auto* targetProg = ctx.program(value.program());
                if (targetProg->name() != expectedName)
                    return false;
                value = ctx.translate(value.program(), targetProg->parent());
            } else if (value.kind() == DeclarationValueKind::Namespace) {
                auto* ns = ctx.getNamespace(value.nsHandle());
                if (ns->name != expectedName)
                    return false;
                if (!ns->parent.has_value())
                    return false;
                value = ns->parent.value();
            } else {
                VERIFY_NOT_REACHED();
            }
        }
        if (value != ctx.m_scopeStack.back().value)
            return false;
        return true;
    }
};

struct CheckExpr {
    virtual ~CheckExpr() = default;

    virtual void check(Context& ctx, ProgramHandle progHandle, Constant value) const = 0;
};
struct ParameterizeExpr : CheckExpr {
    NestedName base;
    std::vector<std::unique_ptr<CheckExpr>> arguments;

    ParameterizeExpr(NestedName base, std::vector<std::unique_ptr<CheckExpr>> args)
        : base(std::move(base)), arguments(std::move(args)) { }

    void check(Context& ctx, ProgramHandle progHandle, Constant value) const override {
        VERIFY(value.kind() == ConstantKind::Parameterize);
        auto parameterize = ctx.program(progHandle)->getParameterize(value);

        VERIFY(base.match(ctx, ctx.translate(progHandle, parameterize.base)));

        VERIFY(parameterize.arguments.size() == arguments.size());
        for (int_t i = 0; i < (int_t)arguments.size(); i++)
            arguments[i]->check(ctx, progHandle, parameterize.arguments[i]);
    }
};
struct LiteralExpr : CheckExpr {
    NestedName literal;

    explicit LiteralExpr(NestedName literal)
        : literal(std::move(literal)) { }

    void check(Context& ctx, ProgramHandle progHandle, Constant value) const override {
        VERIFY(value.kind() == ConstantKind::Program || value.kind() == ConstantKind::Namespace);
        VERIFY(literal.match(ctx, ctx.translate(progHandle, DeclarationValue::fromConstant(value))));
    }
};
struct ParameterExpr : CheckExpr {
    int_t parameterIndex = 0;
    explicit ParameterExpr(int_t index)
        : parameterIndex(index) { }

    void check(Context&, ProgramHandle, Constant value) const override {
        VERIFY(value.kind() == ConstantKind::CopyOfParameter);
        VERIFY((int_t)value.id() == parameterIndex);
    }
};
struct TemplateSignatureExpr : CheckExpr {
    std::unique_ptr<CheckExpr> signatureValue;

    explicit TemplateSignatureExpr(std::unique_ptr<CheckExpr> signatureValue)
        : signatureValue(std::move(signatureValue)) { }

    void check(Context& ctx, ProgramHandle progHandle, Constant value) const override {
        VERIFY(value.kind() == ConstantKind::TemplateSignature$Program
            || value.kind() == ConstantKind::TemplateSignature$Parameterize);
        signatureValue->check(ctx, progHandle, value.templateSignatureBaseConstant());
    }
};
struct FunctionSignatureExpr : CheckExpr {
    std::unique_ptr<CheckExpr> signatureValue;

    explicit FunctionSignatureExpr(std::unique_ptr<CheckExpr> signatureValue)
        : signatureValue(std::move(signatureValue)) { }

    void check(Context& ctx, ProgramHandle progHandle, Constant value) const override {
        VERIFY(value.kind() == ConstantKind::FunctionSignature$Program
            || value.kind() == ConstantKind::FunctionSignature$Parameterize);
        signatureValue->check(ctx, progHandle, value.functionSignatureBaseConstant());
    }
};
struct EnumValueExpr : CheckExpr {
    std::unique_ptr<CheckExpr> typeExpr;
    Word valueName;

    EnumValueExpr(std::unique_ptr<CheckExpr> typeExpr, Word valueName)
        : typeExpr(std::move(typeExpr)), valueName(valueName) { }

    void check(Context& ctx, ProgramHandle progHandle, Constant value) const override {
        VERIFY(value.isEnumValueLiteral());
        auto enumValue = ctx.program(progHandle)->getEnumValue(value);
        typeExpr->check(ctx, progHandle, (Constant)enumValue.enumType);

        auto* enumProg = cast<EnumProgram>(ctx.program(ctx.program(progHandle)->baseProgram(enumValue.enumType).value()));
        VERIFY(enumProg->values[enumValue.valueIndex].name() == valueName);
    }
};

struct CopyOfOpenGlobalExpr : CheckExpr {
    std::unique_ptr<CheckExpr> globalExpr;

    CopyOfOpenGlobalExpr(std::unique_ptr<CheckExpr> globalExpr)
        : globalExpr(std::move(globalExpr)) { }

    void check(Context& ctx, ProgramHandle progHandle, Constant value) const override {
        globalExpr->check(ctx, progHandle, value.copiedGlobal());
    }
};

struct OpenReturnTypeExpr : CheckExpr {
    std::unique_ptr<CheckExpr> fnExpr;

    OpenReturnTypeExpr(std::unique_ptr<CheckExpr> fnExpr)
        : fnExpr(std::move(fnExpr)) { }

    void check(Context& ctx, ProgramHandle progHandle, Constant value) const override {
        fnExpr->check(ctx, progHandle, value.returnTypeOf());
    }
};

struct CheckExprParser {
    Context& context;
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

    NestedName readNestedName() {
        NestedName result;
        for (;;) {
            result.parts.push_back(context.tokenBuffer.wordTable.get(readId()));
            if (!buffer.starts_with("::"))
                break;
            consume("::");
        }
        return result;
    }

    auto parseArguments() {
        std::vector<std::unique_ptr<CheckExpr>> args;
        for (;;) {
            args.push_back(parse());
            if (buffer.starts_with("}"))
                break;
            consume(",");
        }
        return args;
    }

    std::unique_ptr<CheckExpr> parse() {
        if (buffer.starts_with("#")) {
            buffer = buffer.substr(1);
            auto id = readId();
            return std::make_unique<ParameterExpr>(parseInteger(id));
        }
        if (buffer.starts_with("@")) {
            buffer = buffer.substr(1);
            auto id = readId();
            if (id == "templsig") {
                consume("(");
                auto sigExpr = parse();
                consume(")");
                return std::make_unique<TemplateSignatureExpr>(std::move(sigExpr));
            }
            if (id == "fnsig") {
                consume("(");
                auto sigExpr = parse();
                consume(")");
                return std::make_unique<FunctionSignatureExpr>(std::move(sigExpr));
            }
            if (id == "enumValue") {
                consume("(");
                auto typeExpr = parse();
                consume(",");
                auto valueName = readId();
                consume(")");
                return std::make_unique<EnumValueExpr>(std::move(typeExpr), context.tokenBuffer.wordTable.get(valueName));
            }
            if (id == "copyOfOpenGlobal") {
                consume("(");
                auto globalExpr = parse();
                consume(")");
                return std::make_unique<CopyOfOpenGlobalExpr>(std::move(globalExpr));
            }
            if (id == "openReturnType") {
                consume("(");
                auto fnExpr = parse();
                consume(")");
                return std::make_unique<OpenReturnTypeExpr>(std::move(fnExpr));
            }
        }

        auto base = readNestedName();

        if (buffer.starts_with("{")) {
            consume("{");
            auto args = parseArguments();
            consume("}");
            return std::make_unique<ParameterizeExpr>(std::move(base), std::move(args));
        }
        return std::make_unique<LiteralExpr>(std::move(base));
    }
};

}

struct TestInstrumenter : parse::MergedTokenVisitor<TestInstrumenter>, sema::ErrorHandler {
    struct Pair {
        Word key;
        std::string_view value;
    };
    struct Command {
        Word command;
        uint32_t lineIndex;
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

    struct SemanticError {
        std::string name;
        sema::ProgramHandle prog;
    };

    static constexpr auto words = ConstWordStringTable(
        "expect-token", "expect-source-position",
        "line", "column", "expect-identifier",
        "expect-type", "expect-value", "expect-return-type", "expect-impl", "expect-error");

    WordStringTable wordTable { words };
    sema::Context context;
    CommandQueue commandQueue;

    TestInstrumenter(std::span<const sema::ModuleImport> imports, std::string_view source)
        : context { imports, source } { context.errorHandler = this; }

    [[noreturn]] void invalidKey(const Command*, const Pair*) {
        VERIFY_NOT_REACHED();
    }

    void visitWhitespace(parse::WhitespaceInfo info) {
        if (info.tag() == parse::WhitespaceKind::LineComment) {
            handleComment(info, context.tokenBuffer.whitespaceSpelling(info).substr(2));
        }
        if (info.tag() == parse::WhitespaceKind::BlockComment) {
            auto spelling = context.tokenBuffer.whitespaceSpelling(info);
            handleComment(info, spelling.substr(2, spelling.length() - 4));
        }
    }

    void visitToken(parse::TokenInfo tok) {
        // println("L{}: {}", tok.lineNumber(), nameString(tok.kind()));

        if (commandQueue.empty())
            return;
        if (commandQueue.top().command == words["expect-token"]) {
            auto cmd = commandQueue.pop();
            for (const auto& pair : cmd.pairs) {
                if (pair.key == Word())
                    EXPECT_EQ(pair.value, nameString(tok.kind())) << "on line " << (cmd.lineIndex + 1);
                else
                    invalidKey(&cmd, &pair);
            }
        } else if (commandQueue.top().command == words["expect-identifier"]) {
            auto cmd = commandQueue.pop();
            EXPECT_EQ(tok.kind(), parse::TokenKind::IdentifierExpr);
            for (const auto& pair : cmd.pairs) {
                if (pair.key == Word())
                    EXPECT_EQ(pair.value, context.tokenBuffer.wordTable.view(tok.data1<parse::DataKind::Word>()));
                else
                    invalidKey(&cmd, &pair);
            }
        }
    }

    void runTest() {
        parse::Parser parser(context.tokenBuffer.source.data());
        parser.parse(context);
        VERIFY(parser.done());
        VERIFY(context.m_scopeStack.size() == 1);

        visit(context.tokenBuffer);
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
            if (word == words["expect-type"] || word == words["expect-value"] || word == words["expect-return-type"] || word == words["expect-impl"] || word == words["expect-error"]) {
                handleSemanticCommand(word, whitespace, comment);
                return;
            }
            Command command { word, whitespace.lineIndex() };
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

            switch (command.command.toUint()) {
            case words["expect-source-position"].toUint(): {
                for (const auto& pair : command.pairs) {
                    if (pair.key == words["line"])
                        EXPECT_EQ(whitespace.lineNumber(), parseInteger(pair.value));
                    else if (pair.key == words["column"])
                        EXPECT_EQ(whitespace.column(), parseInteger(pair.value));
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
        sema::ProgramHandle programHandle = context.firstDeclarationAfter(whitespace.location()).value();
        sema::Program* program = context.program(programHandle);
        std::optional<SemanticError> semanticError;
        try {
            sema::Generator::signatureCheck(context, programHandle);
        } catch (const SemanticError& semaError) {
            semanticError = semaError;
        }

        if (word == words["expect-error"]) {
            int_t i = 0;
            for (; i < (int_t)comment.size() && isBulkIdentifierChar(comment[i]); i++) { }
            auto expectedError = comment.substr(0, i);
            if (semanticError.has_value()) {
                EXPECT_EQ(semanticError.value().name, expectedError);
            } else {
                program->dump(context);
                FAIL() << "No error occured while expecting " << expectedError << " on line " << whitespace.lineNumber();
            }

            comment = comment.substr(i);
            return;
        }

        //println("-------------------------------");
        //program->dump(context);

        if (semanticError.has_value()) {
            FAIL() << "unexpected semantic error " << semanticError->name << " in " << context.tokenBuffer.wordTable.view(context.program(semanticError->prog)->name());
            return;
        }

        sema::CheckExprParser parser { context, comment };
        auto expr = parser.parse();
        if (word == words["expect-type"])
            expr->check(context, programHandle, (sema::Constant)cast<sema::GlobalProgram>(program)->type());
        if (word == words["expect-value"])
            expr->check(context, programHandle, (sema::Constant)cast<sema::GlobalProgram>(program)->initializer());
        if (word == words["expect-return-type"])
            expr->check(context, programHandle, (sema::Constant)cast<sema::FunctionProgram>(program)->returnType());
        if (word == words["expect-impl"]) {
            ASSERT_TRUE(program->isImpl());
            expr->check(context, programHandle, (sema::Constant)program->selfConstant());
        }
    }

    void handleError(sema::Generator& g, sema::ErrorBase& err) override {
        g.takeTopExpression().release();
        throw SemanticError { err.name(), g.programHandle };
    }

    Command popCommand(Word cause) {
        if (commandQueue.empty()) {
            println("got error '{}' without pending command", wordTable.view(cause));
            VERIFY_NOT_REACHED();
        }
        Command cmd = commandQueue.pop();
        if (cmd.command != cause) {
            println("got error '{}' but pending command is '{}'", wordTable.view(cause), wordTable.view(cmd.command));
            VERIFY_NOT_REACHED();
        }
        return cmd;
    }
};

TEST(Charge, BuiltinModule) {
    namespace fs = std::filesystem;
    fs::path testDir { COMPILER_TEST_DIR };
    auto builtinModule = testDir / "builtins.chrg";
    EXPECT_TRUE(fs::is_regular_file(builtinModule));
    auto sourceBuffer = server::readFile(builtinModule);

    sema::Context context({}, sourceBuffer);
    parse::Parser parser(sourceBuffer.data());
    parser.parse(context);
    VERIFY(parser.done());
    for (auto prog : context.programsInModule(context.thisModule()))
        sema::Generator::signatureCheck(context, prog);

    context.checkBuiltins();
}

TEST(Charge, Files) {
    namespace fs = std::filesystem;
    fs::path testDir { COMPILER_TEST_DIR };

    auto builtinModule = testDir / "builtins.chrg";
    auto builtinModuleSrc = server::readFile(builtinModule);
    sema::Context builtinContext({}, builtinModuleSrc);
    {
        parse::Parser parser(builtinModuleSrc.data());
        parser.parse(builtinContext);
        ASSERT_TRUE(parser.done());
        for (auto prog : builtinContext.programsInModule(builtinContext.thisModule()))
            sema::Generator::signatureCheck(builtinContext, prog);
        builtinContext.checkBuiltins();
    }
    auto builtinExport = builtinContext.exportModule();
    std::span<const sema::ModuleImport> dependencies = { &builtinExport, 1 };

    for (const auto& entry : fs::directory_iterator(testDir / "files")) {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension().string() != ".chrg")
            continue;

        println("File: {}", entry.path().filename().string());
        auto sourceBuffer = server::readFile(entry.path());

        TestInstrumenter test(dependencies, sourceBuffer);
        test.runTest();
    }
}

TEST(Charge, TokenSpelling) {
    std::filesystem::path testDir { COMPILER_TEST_DIR };
    for (auto fileName : { "files/declarations.chrg", "files/expressions.chrg" }) {
        auto filePath = testDir / fileName;
        auto fileSource = server::readFile(filePath);
        // No module dependency need since no semantic analysis will be done
        sema::Context context({}, fileSource);
        parse::Parser parser(fileSource.data());
        parser.parse(context);
        ASSERT_TRUE(parser.done());

        for (const auto& token : context.tokenBuffer.tokens) {
            std::string_view computedSpelling = context.tokenBuffer.tokenSpelling(token);
            std::string_view sourceSpelling = { context.tokenBuffer.sourcePointer(token.location()), computedSpelling.length() };
            EXPECT_EQ(computedSpelling, sourceSpelling);
        }
    }
}

TEST(Charge, DISABLED_Benchmark) {
    namespace fs = std::filesystem;
    fs::path file { COMPILER_TEST_DIR "_old/parser_benchmark.chrg" };
    ASSERT_TRUE(fs::is_regular_file(file));

    auto sourceBuffer = server::readFile(file);

    for (int i = 0; i < 10; i++) {
        sema::Context context({}, sourceBuffer);
        parse::Parser parser(sourceBuffer.data());
        parser.parse(context);
        ASSERT_TRUE(parser.done());
        VERIFY(context.m_scopeStack.size() == 1);
    }
}