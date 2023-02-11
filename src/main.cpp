#include "Lexer.h"
#include "Parser.h"

void test() {
    auto testSingleToken = [](TokenKind kind) {
        Lexer lex(toShortString(kind));
        EXPECT_EQ(lex.tok.kind(), kind);
        lex.advance();
        EXPECT_EQ(lex.tok.kind(), TokenKind::Invalid);
    };

    for (uint32_t kind = 1; kind < std::to_underlying(TokenKind::COUNT); kind++) {
        if (isGoodToken((TokenKind)kind))
            testSingleToken((TokenKind)kind);
    }
}

int main() {
    test();

    {
        Lexer lex("ö");
        EXPECT_EQ(lex.tok.kind(), TokenKind::Invalid);
    }
    {
        Parser par("-+(+{ a, { c, q, }[.a = { a < (b + c) * d }], b, c, d, }++)", true);
        Ptr<Expr> e;
        par.parseBinaryExpr(e);
        fmt::println("used storage {} * 4 bytes", par.storageEndAlign4);
        dump(par.context(), e);
    }
    {
        Parser par(R"str(
            x = a + b;
            y * y;
            x *= x[.a = c];
        )str", true);
        Ptr<Stmt> out[3] = {};
        par.parseStmt(out[0]);
        par.parseStmt(out[1]);
        par.parseStmt(out[2]);
        fmt::println("============ 1 ============");
        dump(par.context(), out[0]);
        fmt::println("============ 2 ============");
        dump(par.context(), out[1]);
        fmt::println("============ 3 ============");
        dump(par.context(), out[2]);
    }
}