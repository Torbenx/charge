#include <verify/language/ParseContext.h>

#include <gtest/gtest.h>

namespace verify::language {

//! Parses the file with a context that is kept, so its globals can be looked at
struct ParsedFileForTest {
    explicit ParsedFileForTest(const char* source) {
        tokens = lexFile(source, context.wordTable);
        ScannedFile scanned = context.scanFile(tokens);
        context.parseScannedFile(scanned);
    }

    std::optional<GlobalName> global(std::string_view name) {
        return context.globals.get(context.wordTable.get(name));
    }

    ir::Database& db() { return context.database.database; }

    ParseContext context;
    std::vector<Token> tokens;
};

TEST(VerifyLanguage, ParseFileFunctions) {
    ParsedDatabase parsed = parseFile(R"(
fn #first($a):
    nop

fn #second($a, $b: bool):
@entry:
    store $a <- $a
@exit:
    nop
)");
    ASSERT_EQ(parsed.functions.size(), 2);
    EXPECT_EQ(parsed.functions[0].name, "first");
    EXPECT_EQ(parsed.functions[1].name, "second");
    EXPECT_EQ(parsed.functions[1].parameterNames, (std::vector<std::string> { "a", "b" }));
    EXPECT_EQ(parsed.functions[1].labels, (std::vector<std::string> { "entry", "exit", "" }));
    EXPECT_FALSE(parsed.functions[0].inlineOwner.has_value());
    EXPECT_FALSE(parsed.functions[1].inlineOwner.has_value());

    EXPECT_EQ(parsed.database.fn(ir::FnHandle(0)).parameterCount(), 1);
    EXPECT_EQ(parsed.database.fn(ir::FnHandle(0)).here().id(), 1);
    EXPECT_EQ(parsed.database.fn(ir::FnHandle(1)).parameterCount(), 2);
    EXPECT_EQ(parsed.database.fn(ir::FnHandle(1)).here().id(), 2);
    EXPECT_TRUE(parsed.typeImplNames.empty());
}

TEST(VerifyLanguage, ParseFileEmpty) {
    ParsedDatabase parsed = parseFile("");
    EXPECT_TRUE(parsed.functions.empty());
    parsed = parseFile("\n\n");
    EXPECT_TRUE(parsed.functions.empty());
}

TEST(VerifyLanguage, ParseFileInlineDefinitions) {
    ParsedFileForTest file(R"(
struct #S($T):
    invariant #:inv($s, $r):
        store $r <- $s
    member #:m($r):
        store $r <- $T
    member #:i invariants #:inv($r):
    @first:
        nop
)");
    const ParsedDatabase& parsed = file.context.database;
    EXPECT_EQ(parsed.typeImplNames, (std::vector<std::string> { "S" }));
    EXPECT_EQ(parsed.invariantNames, (std::vector<std::string> { "S:inv" }));
    EXPECT_EQ(parsed.memberNames, (std::vector<std::string> { "S:m", "S:i" }));

    // The functions are named after what they were defined in, and the parameters of the type come first
    ASSERT_EQ(parsed.functions.size(), 3);
    EXPECT_EQ(parsed.functions[0].name, "S:inv");
    EXPECT_EQ(parsed.functions[0].parameterNames, (std::vector<std::string> { "T", "s", "r" }));
    EXPECT_EQ(parsed.functions[1].name, "S:m");
    EXPECT_EQ(parsed.functions[1].parameterNames, (std::vector<std::string> { "T", "r" }));
    EXPECT_EQ(parsed.functions[2].name, "S:i");
    EXPECT_EQ(parsed.functions[2].labels, (std::vector<std::string> { "first", "" }));

    ASSERT_TRUE(parsed.functions[0].inlineOwner.has_value());
    EXPECT_EQ(parsed.functions[0].inlineOwner->kind(), GlobalKind::Invariant);
    EXPECT_EQ(parsed.functions[0].inlineOwner->id(), 0);
    ASSERT_TRUE(parsed.functions[2].inlineOwner.has_value());
    EXPECT_EQ(parsed.functions[2].inlineOwner->kind(), GlobalKind::Member);
    EXPECT_EQ(parsed.functions[2].inlineOwner->id(), 1);

    ir::Database& db = file.db();
    EXPECT_EQ(db.fn(ir::FnHandle(0)).parameterCount(), 3);
    EXPECT_EQ(db.invariantFn(ir::Invariant(0)).id(), 0);
    EXPECT_EQ(db.typeFn(ir::MemberLiteral(0)).id(), 1);
    EXPECT_EQ(db.typeFn(ir::MemberLiteral(1)).id(), 2);
    EXPECT_EQ(db.containingType(ir::MemberLiteral(1)).id(), 0);

    // The usage map
    EXPECT_TRUE(db.usingInvariants(ir::MemberLiteral(0)).empty());
    ASSERT_EQ(db.usingInvariants(ir::MemberLiteral(1)).size(), 1);
    EXPECT_EQ(db.usingInvariants(ir::MemberLiteral(1))[0].id(), 0);
    ASSERT_EQ(db.usedMembers(ir::Invariant(0)).size(), 1);
    EXPECT_EQ(db.usedMembers(ir::Invariant(0))[0].id(), 1);

    // A name beginning with ':' is the one with the name of the type written in front
    EXPECT_EQ(file.global("S")->kind(), GlobalKind::TypeImpl);
    EXPECT_EQ(file.global("S:inv")->kind(), GlobalKind::Invariant);
    EXPECT_EQ(file.global("S:m")->kind(), GlobalKind::Member);
    EXPECT_EQ(file.global("S:i")->id(), 1);
    // Functions defined inline can't be referred to by name
    EXPECT_FALSE(file.global(":m").has_value());
}

TEST(VerifyLanguage, ParseFileAliases) {
    ParsedFileForTest file(R"(
struct #S:
    invariant #:a = #S::check
    invariant #:b = #::check
    member #:m invariants #:a, #:b = #S_m_type

fn #S::check($s, $r):
    nop

fn #S_m_type($r):
    nop
)");
    ir::Database& db = file.db();
    EXPECT_EQ(db.invariantFn(ir::Invariant(0)).id(), 0);
    EXPECT_EQ(db.invariantFn(ir::Invariant(1)).id(), 0);
    EXPECT_EQ(db.typeFn(ir::MemberLiteral(0)).id(), 1);
    EXPECT_EQ(db.usedMembers(ir::Invariant(1)).size(), 1);
    EXPECT_EQ(db.usingInvariants(ir::MemberLiteral(0)).size(), 2);

    // Functions only named after '=' are not defined inline
    EXPECT_FALSE(file.context.database.functions[0].inlineOwner.has_value());
    EXPECT_FALSE(file.context.database.functions[1].inlineOwner.has_value());
}

TEST(VerifyLanguage, ParseFileWrappedHeaders) {
    // A header continued on another line leaves the parameter list inside of the scope of the
    // continuation, the second phase must continue from there
    ParsedFileForTest file(R"(
struct #S($T,
        $U: bool):
            invariant #:a = #f
            invariant #:b = #f
            member #:m invariants #:a,
                    #:b($r,
                    $q):
                        nop

fn #f($a,
        $b):
            nop
)");
    const ParsedDatabase& parsed = file.context.database;
    ASSERT_EQ(parsed.functions.size(), 2);
    EXPECT_EQ(parsed.functions[0].parameterNames, (std::vector<std::string> { "T", "U", "r", "q" }));
    EXPECT_EQ(parsed.functions[1].parameterNames, (std::vector<std::string> { "a", "b" }));
    EXPECT_EQ(file.db().usingInvariants(ir::MemberLiteral(0)).size(), 2);
}

TEST(VerifyLanguage, ParseFileErrors) {
    // Invariants must be declared before members
    EXPECT_THROW(parseFile(R"(
struct #S:
    member #:m = #f
    invariant #:inv = #f
fn #f():
    nop
)"),
        ParserException);
    // An invariant of a member must be one of the type
    EXPECT_THROW(parseFile(R"(
struct #S:
    member #:m invariants #:inv = #f
fn #f():
    nop
)"),
        ParserException);
    EXPECT_THROW(parseFile(R"(
struct #T:
    invariant #:inv = #f
struct #S:
    member #:m invariants #T:inv = #f
fn #f():
    nop
)"),
        ParserException);
    // An invariant is listed once
    EXPECT_THROW(parseFile(R"(
struct #S:
    invariant #:inv = #f
    member #:m invariants #:inv, #:inv = #f
fn #f():
    nop
)"),
        ParserException);
    // The name after '=' must be a function
    EXPECT_THROW(parseFile(R"(
struct #S:
    member #:m = #g
fn #f():
    nop
)"),
        ParserException);
    EXPECT_THROW(parseFile(R"(
struct #S:
    member #:m = #S
)"),
        ParserException);
    // A global is defined once, whether the type name was left out or not
    EXPECT_THROW(parseFile(R"(
fn #f():
    nop
fn #f():
    nop
)"),
        ParserException);
    EXPECT_THROW(parseFile(R"(
struct #S:
    member #:m = #S:m
fn #S:m():
    nop
)"),
        ParserException);
    EXPECT_THROW(parseFile(R"(
struct #S:
    member #:m = #f
    member #S:m = #f
fn #f():
    nop
)"),
        ParserException);
    EXPECT_THROW(parseFile(R"(
struct #S:
    invariant #:inv = #f
    invariant #:inv = #f
fn #f():
    nop
)"),
        ParserException);
    // A name beginning with ':' is only valid inside of a type
    EXPECT_THROW(parseFile(R"(
fn #:f():
    nop
)"),
        ParserException);
    EXPECT_THROW(parseFile(R"(
struct #:S:
    member #:m = #f
fn #f():
    nop
)"),
        ParserException);
    // A member has a function
    EXPECT_THROW(parseFile(R"(
struct #S:
    member #:m
)"),
        ParserException);
    // TODO: A definition by an expression
    EXPECT_THROW(parseFile(R"(
struct #S:
    member #:m: $x
)"),
        ParserException);
    // A type has no labels
    EXPECT_THROW(parseFile(R"(
struct #S:
@label:
    member #:m = #f
fn #f():
    nop
)"),
        ParserException);
    // One definition per line
    EXPECT_THROW(parseFile(R"(
struct #S:
    member #:m = #f member #:n = #f
fn #f():
    nop
)"),
        ParserException);
    EXPECT_THROW(parseFile(R"(
nop
)"),
        ParserException);
    // The bodies are parsed in the second phase
    EXPECT_THROW(parseFile(R"(
fn #f():
    not_an_instruction
)"),
        ParserException);
    EXPECT_THROW(parseFile(R"(
struct #S:
    member #:m($r):
        jump @nowhere
)"),
        ParserException);
}

}
