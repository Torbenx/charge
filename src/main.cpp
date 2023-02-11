#include "Lexer.h"
#include "Parser.h"

void test() {
    auto testSingleToken = [](TokenKind kind) {
        Lexer lex(toShortString(kind));
        lex.advance();
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

    Lexer lex("ö");
    lex.advance();
    EXPECT_EQ(lex.tok.kind(), TokenKind::Invalid);

    Parser par("-+(+{ a, { c, q, }[.a = { a < (b + c) * d }], b, c, d, }++)", true);
    Ptr<Expr> e;
    par.advance();
    par.parseBinaryExpr(e);
    fmt::println("used storage {} * 4 bytes", par.storageEndAlign4);
    
    dump(par.context(), e);
}