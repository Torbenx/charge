#include "Lexer.h"
#include "Parser.h"

void test() {
    auto testSingleToken = [](TokenKind kind) {
        Lexer lex(exampleString(kind));
        EXPECT_EQ(lex.tok.kind(), kind);
        lex.advance();
        EXPECT_EQ(lex.tok.kind(), TokenKind::EOS);
    };

    for (uint32_t kind = 1; kind < std::to_underlying(TokenKind::COUNT); kind++) {
        if ((TokenKind)kind != TokenKind::Invalid)
            testSingleToken((TokenKind)kind);
    }
}

void testInterpreter();

int main() {
    test();

    {
        Parser par(STContext::create(), "-+(+{ a, { c, q, }[.a = { a < (b + c) * d }], b, a{1,2,3}::foo, d, }++)");
        Ptr<Expr> e;
        par.parseBinaryExpr(e);
        fmt::println("used storage {} * 4 bytes", par.storage->storageEndAlign4);
        dump(par, e);
    }
    {
        Parser par(STContext::create(), R"str({
            let x = a + b;
            let const x = a + b;
            let mut x = a + b;
            const x = a + b;
            mut x = a + b;
        })str");
        Ptr<CompoundStmt> out = {};
        par.parseCompoundStmt(out);
        dump(par, out);
    }
    {
        Parser par(STContext::create(), R"str(
            struct Foo{X: Type} {
                x: list{X} = 5;

                static foo{Y: Type}: Y = 5;

                function bar(z: Z = 3) {
                    if x > 0
                        xNonZero();
                    else if y > 0
                        yNonZero();
                    else
                        bothZero();
                }
            }

            struct Int{} {
                value: int = 0;
            }
            operation Add(i: Int, j: Int) { return Int(i.value + j.value); }
        )str");
        while (par.tok.kind() != TokenKind::EOS) {
            Ptr<Decl> out = {};
            par.parseDecl(out, Parser::DeclParseScope::Namespace);
            dump(par, out);
        }
    }

    testInterpreter();
}