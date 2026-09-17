// A small expression language: tokenizer, Pratt parser, tree-walking
// interpreter, a constant folder and a pretty printer that round-trips the
// abstract syntax tree back to source text.
//
// The language accepted here looks like this:
//
//     let radius = 12;
//     let area = 3141 * radius ** 2 / 1000;
//     if area > 100 then "large" else "small"
//     points[0].x + offset(dx: 4, dy: 5)
//
// Numbers are stored as a scaled integer mantissa plus a decimal exponent,
// because the charge lexer only accepts plain decimal integer literals. The
// value of a literal is  mantissa * 10^exponent, so the source text `12.5`
// lexes into the pair (125, -1) and prints back out as `12.5`.
//
// The parser is a precedence climbing (Pratt) parser: a single table maps a
// token kind to a binding power, and `parse_expression` calls itself through
// the prefix, infix and postfix rules. Those functions are mutually
// recursive, which is the whole point of the design - the grammar is data,
// not control flow.
namespace expression;

// --- Character constants -------------------------------------------------
//
// The tokenizer compares raw characters on nearly every line, so each one
// that matters gets a name up here rather than being spelled inline.

let CHAR_NUL: char = '\0';
let CHAR_SPACE: char = ' ';
let CHAR_TAB: char = '\t';
let CHAR_NEWLINE: char = '\n';
let CHAR_RETURN: char = '\r';
let CHAR_QUOTE: char = '"';
let CHAR_BACKSLASH: char = '\\';
let CHAR_UNDERSCORE: char = '_';
let CHAR_DOT: char = '.';
let CHAR_COMMA: char = ',';
let CHAR_COLON: char = ':';
let CHAR_SEMICOLON: char = ';';
let CHAR_QUESTION: char = '?';
let CHAR_BANG: char = '!';
let CHAR_TILDE: char = '~';
let CHAR_PLUS: char = '+';
let CHAR_MINUS: char = '-';
let CHAR_STAR: char = '*';
let CHAR_SLASH: char = '/';
let CHAR_PERCENT: char = '%';
let CHAR_AMP: char = '&';
let CHAR_PIPE: char = '|';
let CHAR_CARET: char = '^';
let CHAR_LESS: char = '<';
let CHAR_GREATER: char = '>';
let CHAR_EQUAL: char = '=';
let CHAR_LPAREN: char = '(';
let CHAR_RPAREN: char = ')';
let CHAR_LBRACKET: char = '[';
let CHAR_RBRACKET: char = ']';
let CHAR_LBRACE: char = '{';
let CHAR_RBRACE: char = '}';
let CHAR_ZERO: char = '0';
let CHAR_NINE: char = '9';

// --- Spellings -----------------------------------------------------------
//
// Both the tokenizer and the printer need the surface text of every
// operator. Keeping them in one place is what makes the round trip exact.

let TEXT_EMPTY: string = "";
let TEXT_SPACE: string = " ";
let TEXT_NEWLINE: string = "\n";
let TEXT_INDENT: string = "    ";
let TEXT_LPAREN: string = "(";
let TEXT_RPAREN: string = ")";
let TEXT_LBRACKET: string = "[";
let TEXT_RBRACKET: string = "]";
let TEXT_LBRACE: string = "{";
let TEXT_RBRACE: string = "}";
let TEXT_COMMA_SPACE: string = ", ";
let TEXT_SEMICOLON: string = ";";
let TEXT_DOT: string = ".";
let TEXT_COLON_SPACE: string = ": ";
let TEXT_QUESTION_SPACE: string = " ? ";
let TEXT_SPACE_COLON_SPACE: string = " : ";

let WORD_LET: string = "let";
let WORD_IF: string = "if";
let WORD_THEN: string = "then";
let WORD_ELSE: string = "else";
let WORD_WHILE: string = "while";
let WORD_FN: string = "fn";
let WORD_RETURN: string = "return";
let WORD_TRUE: string = "true";
let WORD_FALSE: string = "false";
let WORD_NULL: string = "null";
let WORD_AND: string = "and";
let WORD_OR: string = "or";
let WORD_NOT: string = "not";

// Decimal scaling. A number is mantissa * 10^exponent; the tokenizer refuses
// to keep more than MAX_DIGITS significant digits so the mantissa cannot run
// away on input like 0.0000000000000000001.
let NUMBER_BASE: int = 10;
let MAX_DIGITS: int = 18;
let MIN_EXPONENT: int = -30;
let MAX_EXPONENT: int = 30;
let MAX_DEPTH: int = 96;
let MAX_ARGUMENTS: int = 32;

// --- Token kinds ---------------------------------------------------------
//
// One entry per distinct spelling. The tokenizer does maximal munch, so
// `<<=` has to be tried before `<<` and `<<` before `<`.
choice TokenKind {
    End,
    Invalid,

    Identifier,
    Number,
    Text,
    Comment,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    StarStar,

    Amp,
    Pipe,
    Caret,
    Tilde,
    LessLess,
    GreaterGreater,

    Bang,
    AmpAmp,
    PipePipe,

    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    EqualEqual,
    BangEqual,

    Equal,
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    PercentEqual,
    AmpEqual,
    PipeEqual,
    CaretEqual,
    LessLessEqual,
    GreaterGreaterEqual,

    PlusPlus,
    MinusMinus,

    Question,
    Colon,
    Comma,
    Semicolon,
    Dot,
    Arrow,

    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,

    KeywordLet,
    KeywordIf,
    KeywordThen,
    KeywordElse,
    KeywordWhile,
    KeywordFn,
    KeywordReturn,
    KeywordTrue,
    KeywordFalse,
    KeywordNull,
    KeywordAnd,
    KeywordOr,
    KeywordNot,
}

// --- Node kinds ----------------------------------------------------------

choice NodeKind {
    Invalid,
    NumberLiteral,
    TextLiteral,
    BoolLiteral,
    NullLiteral,
    Variable,
    Unary,
    Binary,
    Postfix,
    Assign,
    Call,
    Index,
    Member,
    Conditional,
    Let,
    Block,
    Loop,
    Return,
    Empty,
}

// --- Value kinds ---------------------------------------------------------

choice ValueKind {
    Nil,
    Bool,
    Number,
    Text,
    List,
    Builtin,
    Failure,
}

// --- Associativity -------------------------------------------------------

choice Associativity {
    None,
    Left,
    Right,
}

// --- Diagnostics ---------------------------------------------------------

choice ErrorCode {
    None,
    UnexpectedCharacter,
    UnterminatedText,
    UnterminatedComment,
    BadEscape,
    TooManyDigits,
    ExponentRange,
    ExpectedExpression,
    ExpectedToken,
    ExpectedIdentifier,
    UnbalancedParen,
    UnbalancedBracket,
    DepthExceeded,
    TooManyArguments,
    NotAssignable,
    UnknownName,
    NotCallable,
    NotIndexable,
    WrongArity,
    DivideByZero,
    TypeMismatch,
}

fn error_text(code: ErrorCode) -> string {
    if (code == ErrorCode.None) {
        return "no error";
    }
    if (code == ErrorCode.UnexpectedCharacter) {
        return "unexpected character in the input";
    }
    if (code == ErrorCode.UnterminatedText) {
        return "unterminated string literal, expected a closing quote";
    }
    if (code == ErrorCode.UnterminatedComment) {
        return "unterminated block comment, expected a closing marker";
    }
    if (code == ErrorCode.BadEscape) {
        return "invalid escape sequence after a backslash";
    }
    if (code == ErrorCode.TooManyDigits) {
        return "number has more significant digits than can be represented";
    }
    if (code == ErrorCode.ExponentRange) {
        return "decimal exponent is out of range";
    }
    if (code == ErrorCode.ExpectedExpression) {
        return "expected an expression";
    }
    if (code == ErrorCode.ExpectedToken) {
        return "expected a different token here";
    }
    if (code == ErrorCode.ExpectedIdentifier) {
        return "expected an identifier";
    }
    if (code == ErrorCode.UnbalancedParen) {
        return "expected a closing parenthesis";
    }
    if (code == ErrorCode.UnbalancedBracket) {
        return "expected a closing bracket";
    }
    if (code == ErrorCode.DepthExceeded) {
        return "expression nests too deeply";
    }
    if (code == ErrorCode.TooManyArguments) {
        return "call has too many arguments";
    }
    if (code == ErrorCode.NotAssignable) {
        return "left hand side of the assignment is not assignable";
    }
    if (code == ErrorCode.UnknownName) {
        return "name is not bound in any enclosing scope";
    }
    if (code == ErrorCode.NotCallable) {
        return "value is not callable";
    }
    if (code == ErrorCode.NotIndexable) {
        return "value cannot be indexed";
    }
    if (code == ErrorCode.WrongArity) {
        return "builtin called with the wrong number of arguments";
    }
    if (code == ErrorCode.DivideByZero) {
        return "division by zero";
    }
    if (code == ErrorCode.TypeMismatch) {
        return "operand types do not match the operator";
    }
    return "unknown error";
}

// --- Source positions ----------------------------------------------------

// Every token carries the position it started at so that a diagnostic can
// point at a line and a column rather than a bare byte offset.
class Position {

    fn advance_over(ref self, c: char) {
        self.offset += 1;
        if (c == CHAR_NEWLINE) {
            self.line += 1;
            self.column = 1;
        } else {
            self.column += 1;
        }
    }

    fn describe(self) -> string {
        var out: string = "line ";
        out.append(to_string(self.line));
        out.append(", column ");
        out.append(to_string(self.column));
        return out;
    }

    var offset: int = 0;
    var line: int = 1;
    var column: int = 1;
}

// A half open range of the source text. The printer never uses spans, but the
// interpreter quotes them back when a runtime error happens.
class Span {

    fn length(self) -> int {
        return self.end.offset - self.begin.offset;
    }

    fn empty(self) -> bool {
        return self.length() <= 0;
    }

    fn text_of(self, source: string) -> string {
        if (self.empty()) {
            return TEXT_EMPTY;
        }
        return source.substr(self.begin.offset, self.length());
    }

    fn describe(self) -> string {
        var out = self.begin.describe();
        if (self.end.line != self.begin.line) {
            out.append(" through ");
            out.append(self.end.describe());
        }
        return out;
    }

    var begin: Position;
    var end: Position;
}

class Diagnostic {

    fn ok(self) -> bool {
        return self.code == ErrorCode.None;
    }

    fn describe(self) -> string {
        var out = self.where_.describe();
        out.append(": ");
        out.append(error_text(self.code));
        if (self.detail.size() > 0) {
            out.append(" (");
            out.append(self.detail);
            out.append(")");
        }
        return out;
    }

    var code: ErrorCode = ErrorCode.None;
    var where_: Position;
    var detail: string;
}

// --- Tokens --------------------------------------------------------------

class Token {

    fn is_end(self) -> bool {
        return self.kind == TokenKind.End;
    }

    fn is_literal(self) -> bool {
        if (self.kind == TokenKind.Number) {
            return true;
        }
        if (self.kind == TokenKind.Text) {
            return true;
        }
        if (self.kind == TokenKind.KeywordTrue) {
            return true;
        }
        if (self.kind == TokenKind.KeywordFalse) {
            return true;
        }
        if (self.kind == TokenKind.KeywordNull) {
            return true;
        }
        return false;
    }

    fn describe(self) -> string {
        var out = token_name(self.kind);
        if (self.text.size() > 0) {
            out.append(" '");
            out.append(self.text);
            out.append("'");
        }
        out.append(" at ");
        out.append(self.span.begin.describe());
        return out;
    }

    var kind: TokenKind = TokenKind.End;
    var text: string;
    var span: Span;
    // Numeric payload, only meaningful when kind is Number. The literal
    // value is mantissa * 10^exponent.
    var mantissa: int = 0;
    var exponent: int = 0;
}

// --- Character classification --------------------------------------------

fn is_space(c: char) => c == CHAR_SPACE or c == CHAR_TAB or c == CHAR_NEWLINE or c == CHAR_RETURN;

fn is_digit(c: char) => c >= '0' and c <= '9';

fn is_lower(c: char) => c >= 'a' and c <= 'z';

fn is_upper(c: char) => c >= 'A' and c <= 'Z';

fn is_letter(c: char) -> bool {
    if (is_lower(c)) {
        return true;
    }
    if (is_upper(c)) {
        return true;
    }
    return c == CHAR_UNDERSCORE;
}

fn is_identifier_start(c: char) -> bool {
    return is_letter(c);
}

fn is_identifier_body(c: char) -> bool {
    if (is_letter(c)) {
        return true;
    }
    return is_digit(c);
}

fn digit_value(c: char) -> int {
    return int(c) - int(CHAR_ZERO);
}

fn to_lower(c: char) -> char {
    if (is_upper(c)) {
        return char(int(c) + 32);
    }
    return c;
}

fn to_upper(c: char) -> char {
    if (is_lower(c)) {
        return char(int(c) - 32);
    }
    return c;
}

// True when the character can begin an operator token. Anything that is not
// a letter, a digit, whitespace or a quote has to be one of these.
fn is_operator_start(c: char) -> bool {
    if (c == CHAR_PLUS or c == CHAR_MINUS or c == CHAR_STAR or c == CHAR_SLASH) {
        return true;
    }
    if (c == CHAR_PERCENT or c == CHAR_AMP or c == CHAR_PIPE or c == CHAR_CARET) {
        return true;
    }
    if (c == CHAR_TILDE or c == CHAR_BANG or c == CHAR_LESS or c == CHAR_GREATER) {
        return true;
    }
    if (c == CHAR_EQUAL or c == CHAR_QUESTION or c == CHAR_COLON or c == CHAR_COMMA) {
        return true;
    }
    if (c == CHAR_SEMICOLON or c == CHAR_DOT) {
        return true;
    }
    return is_bracket(c);
}

fn is_bracket(c: char) -> bool {
    if (c == CHAR_LPAREN or c == CHAR_RPAREN) {
        return true;
    }
    if (c == CHAR_LBRACKET or c == CHAR_RBRACKET) {
        return true;
    }
    if (c == CHAR_LBRACE or c == CHAR_RBRACE) {
        return true;
    }
    return false;
}

// --- Token spellings -----------------------------------------------------
//
// charge has no switch, so the dispatch tables in this file are written as
// long if / else cascades over the token kind. They are ordered the same way
// as the enum so that the two stay easy to compare by eye.

fn token_spelling(kind: TokenKind) -> string {
    if (kind == TokenKind.Plus) { return "+"; }
    if (kind == TokenKind.Minus) { return "-"; }
    if (kind == TokenKind.Star) { return "*"; }
    if (kind == TokenKind.Slash) { return "/"; }
    if (kind == TokenKind.Percent) { return "%"; }
    if (kind == TokenKind.StarStar) { return "**"; }
    if (kind == TokenKind.Amp) { return "&"; }
    if (kind == TokenKind.Pipe) { return "|"; }
    if (kind == TokenKind.Caret) { return "^"; }
    if (kind == TokenKind.Tilde) { return "~"; }
    if (kind == TokenKind.LessLess) { return "<<"; }
    if (kind == TokenKind.GreaterGreater) { return ">>"; }
    if (kind == TokenKind.Bang) { return "!"; }
    if (kind == TokenKind.AmpAmp) { return "&&"; }
    if (kind == TokenKind.PipePipe) { return "||"; }
    if (kind == TokenKind.Less) { return "<"; }
    if (kind == TokenKind.LessEqual) { return "<="; }
    if (kind == TokenKind.Greater) { return ">"; }
    if (kind == TokenKind.GreaterEqual) { return ">="; }
    if (kind == TokenKind.EqualEqual) { return "=="; }
    if (kind == TokenKind.BangEqual) { return "!="; }
    if (kind == TokenKind.Equal) { return "="; }
    if (kind == TokenKind.PlusEqual) { return "+="; }
    if (kind == TokenKind.MinusEqual) { return "-="; }
    if (kind == TokenKind.StarEqual) { return "*="; }
    if (kind == TokenKind.SlashEqual) { return "/="; }
    if (kind == TokenKind.PercentEqual) { return "%="; }
    if (kind == TokenKind.AmpEqual) { return "&="; }
    if (kind == TokenKind.PipeEqual) { return "|="; }
    if (kind == TokenKind.CaretEqual) { return "^="; }
    if (kind == TokenKind.LessLessEqual) { return "<<="; }
    if (kind == TokenKind.GreaterGreaterEqual) { return ">>="; }
    if (kind == TokenKind.PlusPlus) { return "++"; }
    if (kind == TokenKind.MinusMinus) { return "--"; }
    if (kind == TokenKind.Question) { return "?"; }
    if (kind == TokenKind.Colon) { return ":"; }
    if (kind == TokenKind.Comma) { return ","; }
    if (kind == TokenKind.Semicolon) { return ";"; }
    if (kind == TokenKind.Dot) { return "."; }
    if (kind == TokenKind.Arrow) { return "->"; }
    if (kind == TokenKind.LParen) { return "("; }
    if (kind == TokenKind.RParen) { return ")"; }
    if (kind == TokenKind.LBracket) { return "["; }
    if (kind == TokenKind.RBracket) { return "]"; }
    if (kind == TokenKind.LBrace) { return "{"; }
    if (kind == TokenKind.RBrace) { return "}"; }
    if (kind == TokenKind.KeywordLet) { return "let"; }
    if (kind == TokenKind.KeywordIf) { return "if"; }
    if (kind == TokenKind.KeywordThen) { return "then"; }
    if (kind == TokenKind.KeywordElse) { return "else"; }
    if (kind == TokenKind.KeywordWhile) { return "while"; }
    if (kind == TokenKind.KeywordFn) { return "fn"; }
    if (kind == TokenKind.KeywordReturn) { return "return"; }
    if (kind == TokenKind.KeywordTrue) { return "true"; }
    if (kind == TokenKind.KeywordFalse) { return "false"; }
    if (kind == TokenKind.KeywordNull) { return "null"; }
    if (kind == TokenKind.KeywordAnd) { return "and"; }
    if (kind == TokenKind.KeywordOr) { return "or"; }
    if (kind == TokenKind.KeywordNot) { return "not"; }
    return TEXT_EMPTY;
}

// The human readable name, used in diagnostics like "expected ')', found
// end of input".
fn token_name(kind: TokenKind) -> string {
    if (kind == TokenKind.End) { return "end of input"; }
    if (kind == TokenKind.Invalid) { return "invalid token"; }
    if (kind == TokenKind.Identifier) { return "identifier"; }
    if (kind == TokenKind.Number) { return "number"; }
    if (kind == TokenKind.Text) { return "string"; }
    if (kind == TokenKind.Comment) { return "comment"; }
    let spelling = token_spelling(kind);
    if (spelling.size() > 0) {
        return spelling;
    }
    return "token";
}

// Keyword recognition happens after an identifier has been scanned, so the
// tokenizer never needs to back up.
fn keyword_kind(word: string) -> TokenKind {
    if (*word == WORD_LET) { return TokenKind.KeywordLet; }
    if (*word == WORD_IF) { return TokenKind.KeywordIf; }
    if (*word == WORD_THEN) { return TokenKind.KeywordThen; }
    if (*word == WORD_ELSE) { return TokenKind.KeywordElse; }
    if (*word == WORD_WHILE) { return TokenKind.KeywordWhile; }
    if (*word == WORD_FN) { return TokenKind.KeywordFn; }
    if (*word == WORD_RETURN) { return TokenKind.KeywordReturn; }
    if (*word == WORD_TRUE) { return TokenKind.KeywordTrue; }
    if (*word == WORD_FALSE) { return TokenKind.KeywordFalse; }
    if (*word == WORD_NULL) { return TokenKind.KeywordNull; }
    if (*word == WORD_AND) { return TokenKind.KeywordAnd; }
    if (*word == WORD_OR) { return TokenKind.KeywordOr; }
    if (*word == WORD_NOT) { return TokenKind.KeywordNot; }
    return TokenKind.Identifier;
}

fn is_keyword(word: string) -> bool {
    return keyword_kind(word) != TokenKind.Identifier;
}

// --- The precedence table ------------------------------------------------
//
// Binding powers, lowest first. Every infix operator gets one number; a right
// associative operator is parsed by recursing with the same power instead of
// power + 1, which is what `infix_associativity` below decides.
//
//      10  = += -= *= /= %= &= |= ^= <<= >>=       (right)
//      20  ?:                                       (right)
//      30  || or                                    (left)
//      40  && and                                   (left)
//      50  |                                        (left)
//      60  ^                                        (left)
//      70  &                                        (left)
//      80  == !=                                    (left)
//      90  < <= > >=                                (left)
//     100  << >>                                    (left)
//     110  + -                                      (left)
//     120  * / %                                    (left)
//     130  **                                       (right)
//     140  prefix - + ! ~ not ++ --
//     150  postfix ++ -- and the call, index and member suffixes

let PRECEDENCE_ASSIGN: int = 10;
let PRECEDENCE_CONDITIONAL: int = 20;
let PRECEDENCE_LOGICAL_OR: int = 30;
let PRECEDENCE_LOGICAL_AND: int = 40;
let PRECEDENCE_BIT_OR: int = 50;
let PRECEDENCE_BIT_XOR: int = 60;
let PRECEDENCE_BIT_AND: int = 70;
let PRECEDENCE_EQUALITY: int = 80;
let PRECEDENCE_RELATIONAL: int = 90;
let PRECEDENCE_SHIFT: int = 100;
let PRECEDENCE_ADDITIVE: int = 110;
let PRECEDENCE_MULTIPLICATIVE: int = 120;
let PRECEDENCE_POWER: int = 130;
let PRECEDENCE_PREFIX: int = 140;
let PRECEDENCE_POSTFIX: int = 150;
let PRECEDENCE_NONE: int = 0;

// One row of the operator table. The parser does not actually walk a vector
// of these - the cascades below are faster and easier to read - but the
// printer and the test harness both want the table as data.
class OperatorRow {

    fn describe(self) -> string {
        var out = self.spelling;
        out.append(" [");
        out.append(to_string(self.precedence));
        if (self.associativity == Associativity.Right) {
            out.append(", right");
        }
        if (self.associativity == Associativity.Left) {
            out.append(", left");
        }
        out.append("]");
        return out;
    }

    var kind: TokenKind = TokenKind.Invalid;
    var spelling: string;
    var precedence: int = 0;
    var associativity: Associativity = Associativity.Left;
    // True when the operator writes through its left operand, which the
    // constant folder uses to decide whether a subtree is pure.
    var mutating: bool = false;
}

fn operator_table() -> vector(OperatorRow) {
    var rows: vector(OperatorRow);
    rows.push_back(OperatorRow({.kind = TokenKind.Equal, .spelling = "=", .precedence = PRECEDENCE_ASSIGN, .associativity = Associativity.Right, .mutating = true}));
    rows.push_back(OperatorRow({.kind = TokenKind.PlusEqual, .spelling = "+=", .precedence = PRECEDENCE_ASSIGN, .associativity = Associativity.Right, .mutating = true}));
    rows.push_back(OperatorRow({.kind = TokenKind.MinusEqual, .spelling = "-=", .precedence = PRECEDENCE_ASSIGN, .associativity = Associativity.Right, .mutating = true}));
    rows.push_back(OperatorRow({.kind = TokenKind.StarEqual, .spelling = "*=", .precedence = PRECEDENCE_ASSIGN, .associativity = Associativity.Right, .mutating = true}));
    rows.push_back(OperatorRow({.kind = TokenKind.SlashEqual, .spelling = "/=", .precedence = PRECEDENCE_ASSIGN, .associativity = Associativity.Right, .mutating = true}));
    rows.push_back(OperatorRow({.kind = TokenKind.PercentEqual, .spelling = "%=", .precedence = PRECEDENCE_ASSIGN, .associativity = Associativity.Right, .mutating = true}));
    rows.push_back(OperatorRow({.kind = TokenKind.AmpEqual, .spelling = "&=", .precedence = PRECEDENCE_ASSIGN, .associativity = Associativity.Right, .mutating = true}));
    rows.push_back(OperatorRow({.kind = TokenKind.PipeEqual, .spelling = "|=", .precedence = PRECEDENCE_ASSIGN, .associativity = Associativity.Right, .mutating = true}));
    rows.push_back(OperatorRow({.kind = TokenKind.CaretEqual, .spelling = "^=", .precedence = PRECEDENCE_ASSIGN, .associativity = Associativity.Right, .mutating = true}));
    rows.push_back(OperatorRow({.kind = TokenKind.LessLessEqual, .spelling = "<<=", .precedence = PRECEDENCE_ASSIGN, .associativity = Associativity.Right, .mutating = true}));
    rows.push_back(OperatorRow({.kind = TokenKind.GreaterGreaterEqual, .spelling = ">>=", .precedence = PRECEDENCE_ASSIGN, .associativity = Associativity.Right, .mutating = true}));
    rows.push_back(OperatorRow({.kind = TokenKind.PipePipe, .spelling = "||", .precedence = PRECEDENCE_LOGICAL_OR, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.KeywordOr, .spelling = "or", .precedence = PRECEDENCE_LOGICAL_OR, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.AmpAmp, .spelling = "&&", .precedence = PRECEDENCE_LOGICAL_AND, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.KeywordAnd, .spelling = "and", .precedence = PRECEDENCE_LOGICAL_AND, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.Pipe, .spelling = "|", .precedence = PRECEDENCE_BIT_OR, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.Caret, .spelling = "^", .precedence = PRECEDENCE_BIT_XOR, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.Amp, .spelling = "&", .precedence = PRECEDENCE_BIT_AND, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.EqualEqual, .spelling = "==", .precedence = PRECEDENCE_EQUALITY, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.BangEqual, .spelling = "!=", .precedence = PRECEDENCE_EQUALITY, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.Less, .spelling = "<", .precedence = PRECEDENCE_RELATIONAL, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.LessEqual, .spelling = "<=", .precedence = PRECEDENCE_RELATIONAL, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.Greater, .spelling = ">", .precedence = PRECEDENCE_RELATIONAL, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.GreaterEqual, .spelling = ">=", .precedence = PRECEDENCE_RELATIONAL, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.LessLess, .spelling = "<<", .precedence = PRECEDENCE_SHIFT, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.GreaterGreater, .spelling = ">>", .precedence = PRECEDENCE_SHIFT, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.Plus, .spelling = "+", .precedence = PRECEDENCE_ADDITIVE, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.Minus, .spelling = "-", .precedence = PRECEDENCE_ADDITIVE, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.Star, .spelling = "*", .precedence = PRECEDENCE_MULTIPLICATIVE, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.Slash, .spelling = "/", .precedence = PRECEDENCE_MULTIPLICATIVE, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.Percent, .spelling = "%", .precedence = PRECEDENCE_MULTIPLICATIVE, .associativity = Associativity.Left}));
    rows.push_back(OperatorRow({.kind = TokenKind.StarStar, .spelling = "**", .precedence = PRECEDENCE_POWER, .associativity = Associativity.Right}));
    return rows;
}

// The binding power of `kind` when it appears between two operands. Zero
// means "not an infix operator", which is how `parse_expression` knows to
// stop climbing.
fn infix_precedence(kind: TokenKind) -> int {
    if (kind == TokenKind.Equal) { return PRECEDENCE_ASSIGN; }
    if (kind == TokenKind.PlusEqual) { return PRECEDENCE_ASSIGN; }
    if (kind == TokenKind.MinusEqual) { return PRECEDENCE_ASSIGN; }
    if (kind == TokenKind.StarEqual) { return PRECEDENCE_ASSIGN; }
    if (kind == TokenKind.SlashEqual) { return PRECEDENCE_ASSIGN; }
    if (kind == TokenKind.PercentEqual) { return PRECEDENCE_ASSIGN; }
    if (kind == TokenKind.AmpEqual) { return PRECEDENCE_ASSIGN; }
    if (kind == TokenKind.PipeEqual) { return PRECEDENCE_ASSIGN; }
    if (kind == TokenKind.CaretEqual) { return PRECEDENCE_ASSIGN; }
    if (kind == TokenKind.LessLessEqual) { return PRECEDENCE_ASSIGN; }
    if (kind == TokenKind.GreaterGreaterEqual) { return PRECEDENCE_ASSIGN; }
    if (kind == TokenKind.Question) { return PRECEDENCE_CONDITIONAL; }
    if (kind == TokenKind.PipePipe) { return PRECEDENCE_LOGICAL_OR; }
    if (kind == TokenKind.KeywordOr) { return PRECEDENCE_LOGICAL_OR; }
    if (kind == TokenKind.AmpAmp) { return PRECEDENCE_LOGICAL_AND; }
    if (kind == TokenKind.KeywordAnd) { return PRECEDENCE_LOGICAL_AND; }
    if (kind == TokenKind.Pipe) { return PRECEDENCE_BIT_OR; }
    if (kind == TokenKind.Caret) { return PRECEDENCE_BIT_XOR; }
    if (kind == TokenKind.Amp) { return PRECEDENCE_BIT_AND; }
    if (kind == TokenKind.EqualEqual) { return PRECEDENCE_EQUALITY; }
    if (kind == TokenKind.BangEqual) { return PRECEDENCE_EQUALITY; }
    if (kind == TokenKind.Less) { return PRECEDENCE_RELATIONAL; }
    if (kind == TokenKind.LessEqual) { return PRECEDENCE_RELATIONAL; }
    if (kind == TokenKind.Greater) { return PRECEDENCE_RELATIONAL; }
    if (kind == TokenKind.GreaterEqual) { return PRECEDENCE_RELATIONAL; }
    if (kind == TokenKind.LessLess) { return PRECEDENCE_SHIFT; }
    if (kind == TokenKind.GreaterGreater) { return PRECEDENCE_SHIFT; }
    if (kind == TokenKind.Plus) { return PRECEDENCE_ADDITIVE; }
    if (kind == TokenKind.Minus) { return PRECEDENCE_ADDITIVE; }
    if (kind == TokenKind.Star) { return PRECEDENCE_MULTIPLICATIVE; }
    if (kind == TokenKind.Slash) { return PRECEDENCE_MULTIPLICATIVE; }
    if (kind == TokenKind.Percent) { return PRECEDENCE_MULTIPLICATIVE; }
    if (kind == TokenKind.StarStar) { return PRECEDENCE_POWER; }
    return PRECEDENCE_NONE;
}

fn infix_associativity(kind: TokenKind) -> Associativity {
    if (is_assignment_operator(kind)) {
        return Associativity.Right;
    }
    if (kind == TokenKind.Question) {
        return Associativity.Right;
    }
    if (kind == TokenKind.StarStar) {
        return Associativity.Right;
    }
    if (infix_precedence(kind) == PRECEDENCE_NONE) {
        return Associativity.None;
    }
    return Associativity.Left;
}

// The power a prefix operator binds its single operand with. Prefix
// operators are always right associative, so `- - x` parses as `-(-x)`.
fn prefix_precedence(kind: TokenKind) -> int {
    if (kind == TokenKind.Minus) { return PRECEDENCE_PREFIX; }
    if (kind == TokenKind.Plus) { return PRECEDENCE_PREFIX; }
    if (kind == TokenKind.Bang) { return PRECEDENCE_PREFIX; }
    if (kind == TokenKind.KeywordNot) { return PRECEDENCE_PREFIX; }
    if (kind == TokenKind.Tilde) { return PRECEDENCE_PREFIX; }
    if (kind == TokenKind.PlusPlus) { return PRECEDENCE_PREFIX; }
    if (kind == TokenKind.MinusMinus) { return PRECEDENCE_PREFIX; }
    return PRECEDENCE_NONE;
}

fn postfix_precedence(kind: TokenKind) -> int {
    if (kind == TokenKind.PlusPlus) { return PRECEDENCE_POSTFIX; }
    if (kind == TokenKind.MinusMinus) { return PRECEDENCE_POSTFIX; }
    if (kind == TokenKind.Bang) { return PRECEDENCE_POSTFIX; }
    if (kind == TokenKind.LParen) { return PRECEDENCE_POSTFIX; }
    if (kind == TokenKind.LBracket) { return PRECEDENCE_POSTFIX; }
    if (kind == TokenKind.Dot) { return PRECEDENCE_POSTFIX; }
    return PRECEDENCE_NONE;
}

fn is_assignment_operator(kind: TokenKind) -> bool {
    if (kind == TokenKind.Equal) { return true; }
    if (kind == TokenKind.PlusEqual) { return true; }
    if (kind == TokenKind.MinusEqual) { return true; }
    if (kind == TokenKind.StarEqual) { return true; }
    if (kind == TokenKind.SlashEqual) { return true; }
    if (kind == TokenKind.PercentEqual) { return true; }
    if (kind == TokenKind.AmpEqual) { return true; }
    if (kind == TokenKind.PipeEqual) { return true; }
    if (kind == TokenKind.CaretEqual) { return true; }
    if (kind == TokenKind.LessLessEqual) { return true; }
    if (kind == TokenKind.GreaterGreaterEqual) { return true; }
    return false;
}

// A compound assignment such as `a += b` desugars to `a = a + b`, so the
// evaluator needs the plain operator that hides inside it.
fn compound_base_operator(kind: TokenKind) -> TokenKind {
    if (kind == TokenKind.PlusEqual) { return TokenKind.Plus; }
    if (kind == TokenKind.MinusEqual) { return TokenKind.Minus; }
    if (kind == TokenKind.StarEqual) { return TokenKind.Star; }
    if (kind == TokenKind.SlashEqual) { return TokenKind.Slash; }
    if (kind == TokenKind.PercentEqual) { return TokenKind.Percent; }
    if (kind == TokenKind.AmpEqual) { return TokenKind.Amp; }
    if (kind == TokenKind.PipeEqual) { return TokenKind.Pipe; }
    if (kind == TokenKind.CaretEqual) { return TokenKind.Caret; }
    if (kind == TokenKind.LessLessEqual) { return TokenKind.LessLess; }
    if (kind == TokenKind.GreaterGreaterEqual) { return TokenKind.GreaterGreater; }
    return TokenKind.Invalid;
}

// Operators whose right hand side may be skipped entirely.
fn is_short_circuit(kind: TokenKind) -> bool {
    if (kind == TokenKind.AmpAmp or kind == TokenKind.KeywordAnd) {
        return true;
    }
    if (kind == TokenKind.PipePipe or kind == TokenKind.KeywordOr) {
        return true;
    }
    return false;
}

fn is_comparison(kind: TokenKind) -> bool {
    let power = infix_precedence(kind);
    if (power == PRECEDENCE_EQUALITY) {
        return true;
    }
    return power == PRECEDENCE_RELATIONAL;
}

fn is_arithmetic(kind: TokenKind) -> bool {
    let power = infix_precedence(kind);
    if (power == PRECEDENCE_ADDITIVE) {
        return true;
    }
    if (power == PRECEDENCE_MULTIPLICATIVE) {
        return true;
    }
    return power == PRECEDENCE_POWER;
}

fn is_bitwise(kind: TokenKind) -> bool {
    let power = infix_precedence(kind);
    if (power == PRECEDENCE_BIT_OR or power == PRECEDENCE_BIT_XOR) {
        return true;
    }
    if (power == PRECEDENCE_BIT_AND) {
        return true;
    }
    return power == PRECEDENCE_SHIFT;
}

// --- The tokenizer -------------------------------------------------------
//
// A hand written scanner over a string. It never allocates for a token that
// it is going to throw away, and every token records the span it covers so
// that the printer can reproduce the original spacing if it wants to.
class Tokenizer {

    fn at_end(self) -> bool {
        return self.cursor.offset >= self.source.size();
    }

    fn remaining(self) -> int {
        return self.source.size() - self.cursor.offset;
    }

    fn peek(self) -> char {
        if (self.at_end()) {
            return CHAR_NUL;
        }
        return self.source.at(self.cursor.offset);
    }

    fn peek_at(self, ahead: int) -> char {
        let index = self.cursor.offset + ahead;
        if (index >= self.source.size()) {
            return CHAR_NUL;
        }
        return self.source.at(index);
    }

    fn advance(ref self) -> char {
        let c = self.peek();
        self.cursor.advance_over(c);
        return c;
    }

    // Consume `c` if it is the next character. Returns whether it was there,
    // which is what makes the maximal munch cascade below read well.
    fn eat(ref self, c: char) -> bool {
        if (self.peek() == c) {
            Discard(self.advance());
            return true;
        }
        return false;
    }

    fn fail(ref self, code: ErrorCode, detail: string) -> Token {
        if (self.problem.ok()) {
            self.problem.code = code;
            self.problem.where_ = self.cursor;
            self.problem.detail = *detail;
        }
        var bad = Token({.kind = TokenKind.Invalid, .text = *detail});
        bad.span.begin = self.cursor;
        bad.span.end = self.cursor;
        return bad;
    }

    fn make(self, kind: TokenKind, begin: Position) -> Token {
        var out = Token({.kind = kind});
        out.span.begin = *begin;
        out.span.end = self.cursor;
        out.text = self.source.substr(begin.offset, self.cursor.offset - begin.offset);
        return out;
    }

    // --- Trivia ----------------------------------------------------------

    // Whitespace, line comments and nestable block comments. When
    // `keep_comments` is set the caller gets the comment back as a token
    // instead, which is what the formatter mode wants.
    fn skip_trivia(ref self) {
        while (true) {
            while (not self.at_end() and is_space(self.peek())) {
                Discard(self.advance());
            }
            if (self.at_line_comment()) {
                self.skip_line_comment();
            } else {
                if (self.at_block_comment()) {
                    self.skip_block_comment();
                } else {
                    break;
                }
            }
        }
    }

    fn at_line_comment(self) -> bool {
        if (self.peek() != CHAR_SLASH) {
            return false;
        }
        return self.peek_at(1) == CHAR_SLASH;
    }

    fn at_block_comment(self) -> bool {
        if (self.peek() != CHAR_SLASH) {
            return false;
        }
        return self.peek_at(1) == CHAR_STAR;
    }

    fn skip_line_comment(ref self) {
        Discard(self.advance());
        Discard(self.advance());
        while (not self.at_end() and self.peek() != CHAR_NEWLINE) {
            Discard(self.advance());
        }
    }

    // Block comments nest, so a commented out region that already contains a
    // comment still ends where the reader expects it to.
    fn skip_block_comment(ref self) {
        Discard(self.advance());
        Discard(self.advance());
        var depth: int = 1;
        while (depth > 0) {
            if (self.at_end()) {
                self.problem.code = ErrorCode.UnterminatedComment;
                self.problem.where_ = self.cursor;
                return;
            }
            if (self.at_block_comment()) {
                Discard(self.advance());
                Discard(self.advance());
                depth += 1;
            } else {
                if (self.peek() == CHAR_STAR and self.peek_at(1) == CHAR_SLASH) {
                    Discard(self.advance());
                    Discard(self.advance());
                    depth -= 1;
                } else {
                    Discard(self.advance());
                }
            }
        }
    }

    fn scan_comment(ref self) -> Token {
        let begin = self.cursor;
        if (self.at_line_comment()) {
            self.skip_line_comment();
        } else {
            self.skip_block_comment();
        }
        return self.make(TokenKind.Comment, begin);
    }

    // --- Words -----------------------------------------------------------

    fn scan_identifier(ref self) -> Token {
        let begin = self.cursor;
        Discard(self.advance());
        while (not self.at_end() and is_identifier_body(self.peek())) {
            Discard(self.advance());
        }
        var word = self.make(TokenKind.Identifier, begin);
        word.kind = keyword_kind(word.text);
        return word;
    }

    // --- Numbers ---------------------------------------------------------
    //
    // The literal grammar is
    //
    //     digits [ '.' digits ] [ ('e'|'E') ['+'|'-'] digits ]
    //
    // and the result is the pair (mantissa, exponent) such that the value is
    // mantissa * 10^exponent. Digits past MAX_DIGITS are dropped and the
    // exponent is bumped instead, so `123456789012345678901` still lexes.
    fn scan_number(ref self) -> Token {
        let begin = self.cursor;
        var mantissa: int = 0;
        var exponent: int = 0;
        var digits: int = 0;
        var seen_any = false;

        while (not self.at_end() and is_digit(self.peek())) {
            let d = digit_value(self.advance());
            seen_any = true;
            if (digits < MAX_DIGITS) {
                mantissa = mantissa * NUMBER_BASE + d;
                if (mantissa != 0) {
                    digits += 1;
                }
            } else {
                exponent += 1;
            }
        }

        if (self.peek() == CHAR_DOT and is_digit(self.peek_at(1))) {
            Discard(self.advance());
            while (not self.at_end() and is_digit(self.peek())) {
                let d = digit_value(self.advance());
                seen_any = true;
                if (digits < MAX_DIGITS) {
                    mantissa = mantissa * NUMBER_BASE + d;
                    exponent -= 1;
                    digits += 1;
                }
            }
        }

        if (not seen_any) {
            return self.fail(ErrorCode.UnexpectedCharacter, "expected at least one digit");
        }

        if (self.peek() == 'e' or self.peek() == 'E') {
            let saved = self.cursor;
            Discard(self.advance());
            var sign: int = 1;
            if (self.eat(CHAR_MINUS)) {
                sign = -1;
            } else {
                Discard(self.eat(CHAR_PLUS));
            }
            if (is_digit(self.peek())) {
                var written: int = 0;
                while (not self.at_end() and is_digit(self.peek())) {
                    written = written * NUMBER_BASE + digit_value(self.advance());
                    if (written > MAX_EXPONENT * NUMBER_BASE) {
                        return self.fail(ErrorCode.ExponentRange, "exponent is too large");
                    }
                }
                exponent += sign * written;
            } else {
                // Not an exponent after all, `2eggs` is `2` then a word.
                self.cursor = saved;
            }
        }

        if (exponent > MAX_EXPONENT or exponent < MIN_EXPONENT) {
            return self.fail(ErrorCode.ExponentRange, "decimal exponent leaves the supported range");
        }

        var number = self.make(TokenKind.Number, begin);
        number.mantissa = mantissa;
        number.exponent = exponent;
        return number;
    }

    // --- Strings ---------------------------------------------------------

    fn scan_text(ref self) -> Token {
        let begin = self.cursor;
        Discard(self.advance());
        var decoded: string = "";
        while (true) {
            if (self.at_end()) {
                return self.fail(ErrorCode.UnterminatedText, "reached end of input inside a string");
            }
            let c = self.advance();
            if (c == CHAR_QUOTE) {
                break;
            }
            if (c != CHAR_BACKSLASH) {
                decoded.push_back(c);
            } else {
                if (self.at_end()) {
                    return self.fail(ErrorCode.UnterminatedText, "trailing backslash");
                }
                let marker = self.advance();
                let plain = decode_escape(marker);
                if (plain != CHAR_NUL) {
                    decoded.push_back(plain);
                } else {
                    var detail: string = "unknown escape marker ";
                    detail.push_back(marker);
                    return self.fail(ErrorCode.BadEscape, detail);
                }
            }
        }
        var out = self.make(TokenKind.Text, begin);
        out.text = decoded;
        return out;
    }

    // --- Operators -------------------------------------------------------
    //
    // Maximal munch: three character operators are tried before two and two
    // before one. This is the densest cascade in the file and it is the part
    // the parser leans on hardest.
    fn scan_operator(ref self) -> Token {
        let begin = self.cursor;
        let c = self.advance();

        if (c == CHAR_PLUS) {
            if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.PlusEqual, begin); }
            if (self.eat(CHAR_PLUS)) { return self.make(TokenKind.PlusPlus, begin); }
            return self.make(TokenKind.Plus, begin);
        }
        if (c == CHAR_MINUS) {
            if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.MinusEqual, begin); }
            if (self.eat(CHAR_MINUS)) { return self.make(TokenKind.MinusMinus, begin); }
            if (self.eat(CHAR_GREATER)) { return self.make(TokenKind.Arrow, begin); }
            return self.make(TokenKind.Minus, begin);
        }
        if (c == CHAR_STAR) {
            if (self.eat(CHAR_STAR)) { return self.make(TokenKind.StarStar, begin); }
            if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.StarEqual, begin); }
            return self.make(TokenKind.Star, begin);
        }
        if (c == CHAR_SLASH) {
            if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.SlashEqual, begin); }
            return self.make(TokenKind.Slash, begin);
        }
        if (c == CHAR_PERCENT) {
            if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.PercentEqual, begin); }
            return self.make(TokenKind.Percent, begin);
        }
        if (c == CHAR_AMP) {
            if (self.eat(CHAR_AMP)) { return self.make(TokenKind.AmpAmp, begin); }
            if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.AmpEqual, begin); }
            return self.make(TokenKind.Amp, begin);
        }
        if (c == CHAR_PIPE) {
            if (self.eat(CHAR_PIPE)) { return self.make(TokenKind.PipePipe, begin); }
            if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.PipeEqual, begin); }
            return self.make(TokenKind.Pipe, begin);
        }
        if (c == CHAR_CARET) {
            if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.CaretEqual, begin); }
            return self.make(TokenKind.Caret, begin);
        }
        if (c == CHAR_TILDE) {
            return self.make(TokenKind.Tilde, begin);
        }
        if (c == CHAR_BANG) {
            if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.BangEqual, begin); }
            return self.make(TokenKind.Bang, begin);
        }
        if (c == CHAR_LESS) {
            if (self.eat(CHAR_LESS)) {
                if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.LessLessEqual, begin); }
                return self.make(TokenKind.LessLess, begin);
            }
            if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.LessEqual, begin); }
            return self.make(TokenKind.Less, begin);
        }
        if (c == CHAR_GREATER) {
            if (self.eat(CHAR_GREATER)) {
                if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.GreaterGreaterEqual, begin); }
                return self.make(TokenKind.GreaterGreater, begin);
            }
            if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.GreaterEqual, begin); }
            return self.make(TokenKind.Greater, begin);
        }
        if (c == CHAR_EQUAL) {
            if (self.eat(CHAR_EQUAL)) { return self.make(TokenKind.EqualEqual, begin); }
            return self.make(TokenKind.Equal, begin);
        }
        if (c == CHAR_QUESTION) {
            return self.make(TokenKind.Question, begin);
        }
        if (c == CHAR_COLON) {
            return self.make(TokenKind.Colon, begin);
        }
        if (c == CHAR_COMMA) {
            return self.make(TokenKind.Comma, begin);
        }
        if (c == CHAR_SEMICOLON) {
            return self.make(TokenKind.Semicolon, begin);
        }
        if (c == CHAR_DOT) {
            return self.make(TokenKind.Dot, begin);
        }
        if (c == CHAR_LPAREN) { return self.make(TokenKind.LParen, begin); }
        if (c == CHAR_RPAREN) { return self.make(TokenKind.RParen, begin); }
        if (c == CHAR_LBRACKET) { return self.make(TokenKind.LBracket, begin); }
        if (c == CHAR_RBRACKET) { return self.make(TokenKind.RBracket, begin); }
        if (c == CHAR_LBRACE) { return self.make(TokenKind.LBrace, begin); }
        if (c == CHAR_RBRACE) { return self.make(TokenKind.RBrace, begin); }

        var detail: string = "stray byte ";
        detail.append(to_string(int(c)));
        return self.fail(ErrorCode.UnexpectedCharacter, detail);
    }

    // --- The driver ------------------------------------------------------

    fn next_token(ref self) -> Token {
        if (self.keep_comments) {
            while (not self.at_end() and is_space(self.peek())) {
                Discard(self.advance());
            }
            if (self.at_line_comment() or self.at_block_comment()) {
                return self.scan_comment();
            }
        } else {
            self.skip_trivia();
        }

        let begin = self.cursor;
        if (self.at_end()) {
            var done = Token({.kind = TokenKind.End});
            done.span.begin = begin;
            done.span.end = begin;
            return done;
        }

        let c = self.peek();
        if (is_identifier_start(c)) {
            return self.scan_identifier();
        }
        if (is_digit(c)) {
            return self.scan_number();
        }
        if (c == CHAR_DOT and is_digit(self.peek_at(1))) {
            return self.scan_number();
        }
        if (c == CHAR_QUOTE) {
            return self.scan_text();
        }
        if (is_operator_start(c)) {
            return self.scan_operator();
        }

        Discard(self.advance());
        var detail: string = "character ";
        detail.push_back(c);
        return self.fail(ErrorCode.UnexpectedCharacter, detail);
    }

    var source: string;
    var cursor: Position;
    var problem: Diagnostic;
    var keep_comments: bool = false;
}

// The eight single character escapes the string scanner understands. NUL is
// the "not an escape" answer, which is fine because a source file may not
// contain a raw NUL anyway.
fn decode_escape(marker: char) -> char {
    if (marker == 'n') { return CHAR_NEWLINE; }
    if (marker == 't') { return CHAR_TAB; }
    if (marker == 'r') { return CHAR_RETURN; }
    if (marker == CHAR_QUOTE) { return CHAR_QUOTE; }
    if (marker == CHAR_BACKSLASH) { return CHAR_BACKSLASH; }
    if (marker == '0') { return CHAR_SPACE; }
    if (marker == 'e') { return CHAR_SPACE; }
    return CHAR_NUL;
}

fn encode_escape(c: char) -> string {
    if (c == CHAR_QUOTE) { return "\\\""; }
    if (c == CHAR_BACKSLASH) { return "\\\\"; }
    if (c == CHAR_NEWLINE) { return "\\n"; }
    if (c == CHAR_TAB) { return "\\t"; }
    if (c == CHAR_RETURN) { return "\\r"; }
    return TEXT_EMPTY;
}

// Run the tokenizer to completion. Used by the test harness and by the
// formatter, both of which want the whole stream up front.
fn tokenize(source: string) -> vector(Token) {
    var scanner = Tokenizer({.source = *source});
    var out: vector(Token);
    while (true) {
        let token = scanner.next_token();
        out.push_back(token);
        if (token.kind == TokenKind.End) {
            break;
        }
        if (token.kind == TokenKind.Invalid) {
            break;
        }
    }
    return out;
}

// --- The syntax tree -----------------------------------------------------
//
// Nodes live in one flat arena and refer to each other by index. That keeps
// every node the same size, makes the constant folder able to rewrite a
// subtree in place, and sidesteps the ownership question that a tree of
// unique pointers would raise.
//
// Index 0 is always the invalid node, so a zero child means "absent".

let NO_NODE: int = 0;

class Node {

    fn is_literal(self) -> bool {
        if (self.kind == NodeKind.NumberLiteral) { return true; }
        if (self.kind == NodeKind.TextLiteral) { return true; }
        if (self.kind == NodeKind.BoolLiteral) { return true; }
        if (self.kind == NodeKind.NullLiteral) { return true; }
        return false;
    }

    fn is_leaf(self) -> bool {
        if (self.is_literal()) {
            return true;
        }
        if (self.kind == NodeKind.Variable) { return true; }
        if (self.kind == NodeKind.Empty) { return true; }
        return false;
    }

    // An lvalue is anything an assignment may be written through.
    fn is_assignable(self) -> bool {
        if (self.kind == NodeKind.Variable) { return true; }
        if (self.kind == NodeKind.Index) { return true; }
        if (self.kind == NodeKind.Member) { return true; }
        return false;
    }

    fn child_count(self) -> int {
        var total: int = 0;
        if (self.lhs != NO_NODE) { total += 1; }
        if (self.rhs != NO_NODE) { total += 1; }
        if (self.extra != NO_NODE) { total += 1; }
        return total + self.parts.size();
    }

    fn kind_name(self) -> string {
        return node_kind_name(self.kind);
    }

    var kind: NodeKind = NodeKind.Invalid;
    // The operator for Unary, Binary, Postfix and Assign nodes.
    var op: TokenKind = TokenKind.Invalid;
    // Up to three positional children: the operand or condition, the second
    // operand or the then branch, and the else branch.
    var lhs: int = 0;
    var rhs: int = 0;
    var extra: int = 0;
    // Variable width children: call arguments and block statements.
    var parts: vector(int);
    // Literal payloads.
    var mantissa: int = 0;
    var exponent: int = 0;
    var text: string;
    var flag: bool = false;
    var span: Span;
}

fn node_kind_name(kind: NodeKind) -> string {
    if (kind == NodeKind.Invalid) { return "invalid"; }
    if (kind == NodeKind.NumberLiteral) { return "number"; }
    if (kind == NodeKind.TextLiteral) { return "string"; }
    if (kind == NodeKind.BoolLiteral) { return "bool"; }
    if (kind == NodeKind.NullLiteral) { return "null"; }
    if (kind == NodeKind.Variable) { return "variable"; }
    if (kind == NodeKind.Unary) { return "unary"; }
    if (kind == NodeKind.Binary) { return "binary"; }
    if (kind == NodeKind.Postfix) { return "postfix"; }
    if (kind == NodeKind.Assign) { return "assign"; }
    if (kind == NodeKind.Call) { return "call"; }
    if (kind == NodeKind.Index) { return "index"; }
    if (kind == NodeKind.Member) { return "member"; }
    if (kind == NodeKind.Conditional) { return "conditional"; }
    if (kind == NodeKind.Let) { return "let"; }
    if (kind == NodeKind.Block) { return "block"; }
    if (kind == NodeKind.Loop) { return "loop"; }
    if (kind == NodeKind.Return) { return "return"; }
    if (kind == NodeKind.Empty) { return "empty"; }
    return "unknown";
}

// The arena. Every function that builds or walks a tree takes one of these
// by reference; nothing else owns a node.
class Arena {

    fn size(self) -> int {
        return self.nodes.size();
    }

    fn valid(self, index: int) -> bool {
        if (index <= NO_NODE) {
            return false;
        }
        return index < self.nodes.size();
    }

    fn at(ref self, index: int) -> ref Node {
        Assert(index >= 0);
        if (not self.valid(index)) {
            return self.nodes[0];
        }
        return self.nodes[index];
    }

    fn kind_of(self, index: int) -> NodeKind {
        if (not self.valid(index)) {
            return NodeKind.Invalid;
        }
        return self.nodes[index].kind;
    }

    fn operator_of(self, index: int) -> TokenKind {
        if (not self.valid(index)) {
            return TokenKind.Invalid;
        }
        return self.nodes[index].op;
    }

    fn add(ref self, node: Node) -> int {
        self.nodes.push_back(move(node));
        return self.nodes.size() - 1;
    }

    fn add_number(ref self, mantissa: int, exponent: int) -> int {
        return self.add(Node({.kind = NodeKind.NumberLiteral, .mantissa = mantissa, .exponent = exponent}));
    }

    fn add_text(ref self, text: string) -> int {
        return self.add(Node({.kind = NodeKind.TextLiteral, .text = *text}));
    }

    fn add_bool(ref self, value: bool) -> int {
        return self.add(Node({.kind = NodeKind.BoolLiteral, .flag = value}));
    }

    fn add_null(ref self) -> int {
        return self.add(Node({.kind = NodeKind.NullLiteral}));
    }

    fn add_variable(ref self, name: string) -> int {
        return self.add(Node({.kind = NodeKind.Variable, .text = *name}));
    }

    fn add_unary(ref self, op: TokenKind, operand: int) -> int {
        return self.add(Node({.kind = NodeKind.Unary, .op = op, .lhs = operand}));
    }

    fn add_binary(ref self, op: TokenKind, left: int, right: int) -> int {
        return self.add(Node({.kind = NodeKind.Binary, .op = op, .lhs = left, .rhs = right}));
    }

    fn add_conditional(ref self, condition: int, whenever: int, otherwise: int) -> int {
        return self.add(Node({.kind = NodeKind.Conditional, .lhs = condition, .rhs = whenever, .extra = otherwise}));
    }

    // Replace a node in place. The folder uses this so that every parent
    // that already points at `index` sees the rewritten value.
    fn replace(ref self, index: int, node: Node) {
        if (not self.valid(index)) {
            return;
        }
        let ref slot: auto = self.nodes[index];
        slot.kind = node.kind;
        slot.op = node.op;
        slot.lhs = node.lhs;
        slot.rhs = node.rhs;
        slot.extra = node.extra;
        slot.mantissa = node.mantissa;
        slot.exponent = node.exponent;
        slot.text = node.text;
        slot.flag = node.flag;
    }

    fn replace_with_number(ref self, index: int, mantissa: int, exponent: int) {
        if (not self.valid(index)) {
            return;
        }
        let ref slot: auto = self.nodes[index];
        slot.kind = NodeKind.NumberLiteral;
        slot.op = TokenKind.Invalid;
        slot.lhs = NO_NODE;
        slot.rhs = NO_NODE;
        slot.extra = NO_NODE;
        slot.mantissa = mantissa;
        slot.exponent = exponent;
        slot.parts.clear();
    }

    fn replace_with_bool(ref self, index: int, value: bool) {
        if (not self.valid(index)) {
            return;
        }
        let ref slot: auto = self.nodes[index];
        slot.kind = NodeKind.BoolLiteral;
        slot.op = TokenKind.Invalid;
        slot.lhs = NO_NODE;
        slot.rhs = NO_NODE;
        slot.extra = NO_NODE;
        slot.flag = value;
        slot.parts.clear();
    }

    // Splice `source` into this arena, returning the index of the copy. Used
    // when a fold turns `if true then a else b` into just `a`: the surviving
    // branch has to keep its own children.
    fn adopt(ref self, index: int) -> int {
        if (not self.valid(index)) {
            return NO_NODE;
        }
        var copy = Node({.kind = self.nodes[index].kind, .op = self.nodes[index].op});
        copy.mantissa = self.nodes[index].mantissa;
        copy.exponent = self.nodes[index].exponent;
        copy.text = self.nodes[index].text;
        copy.flag = self.nodes[index].flag;
        copy.lhs = self.nodes[index].lhs;
        copy.rhs = self.nodes[index].rhs;
        copy.extra = self.nodes[index].extra;
        for (child in self.nodes[index].parts) {
            copy.parts.push_back(child);
        }
        return self.add(move(copy));
    }

    fn clear(ref self) {
        self.nodes.clear();
        self.nodes.push_back(Node({.kind = NodeKind.Invalid}));
    }

    var nodes: vector(Node);
}

fn make_arena() -> Arena {
    var arena: Arena;
    arena.clear();
    return arena;
}

// --- The parser ----------------------------------------------------------
//
// Precedence climbing. `parse_expression(power)` parses a prefix form and
// then keeps absorbing infix and postfix operators for as long as their
// binding power is at least `power`. The prefix, infix and postfix helpers
// all call back into `parse_expression`, so the whole family is mutually
// recursive and the shape of the grammar lives in the precedence table
// rather than in the control flow.
class Parser {

    // --- Token access ----------------------------------------------------

    fn peek(self) -> ref const Token {
        return self.lookahead;
    }

    fn peek_kind(self) -> TokenKind {
        return self.lookahead.kind;
    }

    fn at(self, kind: TokenKind) -> bool {
        return self.lookahead.kind == kind;
    }

    fn at_end(self) -> bool {
        return self.lookahead.kind == TokenKind.End;
    }

    fn advance(ref self) -> Token {
        var previous = self.lookahead;
        self.lookahead = self.scanner.next_token();
        if (self.lookahead.kind == TokenKind.Invalid) {
            self.adopt_scanner_error();
        }
        return previous;
    }

    fn eat(ref self, kind: TokenKind) -> bool {
        if (self.at(kind)) {
            Discard(self.advance());
            return true;
        }
        return false;
    }

    fn expect(ref self, kind: TokenKind) -> bool {
        if (self.eat(kind)) {
            return true;
        }
        var detail: string = "expected ";
        detail.append(token_name(kind));
        detail.append(", found ");
        detail.append(token_name(self.lookahead.kind));
        self.report(ErrorCode.ExpectedToken, detail);
        return false;
    }

    fn adopt_scanner_error(ref self) {
        if (self.problem.ok()) {
            self.problem = self.scanner.problem;
        }
    }

    fn report(ref self, code: ErrorCode, detail: string) {
        if (self.problem.ok()) {
            self.problem.code = code;
            self.problem.where_ = self.lookahead.span.begin;
            self.problem.detail = *detail;
        }
    }

    fn failed(self) -> bool {
        return not self.problem.ok();
    }

    // Recover by skipping to the next statement boundary. Without this a
    // single bad token would produce one error per remaining token.
    fn recover(ref self) {
        while (not self.at_end()) {
            if (self.at(TokenKind.Semicolon)) {
                Discard(self.advance());
                return;
            }
            if (self.at(TokenKind.RBrace)) {
                return;
            }
            Discard(self.advance());
        }
    }

    // --- Depth guard -----------------------------------------------------

    fn enter(ref self) -> bool {
        self.depth += 1;
        if (self.depth > MAX_DEPTH) {
            self.report(ErrorCode.DepthExceeded, "nesting limit reached");
            return false;
        }
        return true;
    }

    fn leave(ref self) {
        self.depth -= 1;
    }

    // --- Entry points ----------------------------------------------------

    fn start(ref self, source: string) {
        self.scanner = Tokenizer({.source = *source});
        self.tree.clear();
        self.problem = Diagnostic();
        self.depth = 0;
        self.lookahead = self.scanner.next_token();
    }

    // A whole program is a sequence of statements, gathered into one block.
    fn parse_program(ref self, source: string) -> int {
        self.start(source);
        var block = Node({.kind = NodeKind.Block});
        block.span.begin = self.lookahead.span.begin;
        while (not self.at_end() and not self.failed()) {
            let statement = self.parse_statement();
            if (statement != NO_NODE) {
                block.parts.push_back(statement);
            } else {
                self.recover();
            }
        }
        block.span.end = self.lookahead.span.end;
        return self.tree.add(move(block));
    }

    // The convenience entry point for a single expression, which is what the
    // read eval print loop and most of the tests use.
    fn parse_source(ref self, source: string) -> int {
        self.start(source);
        let root = self.parse_expression(PRECEDENCE_NONE);
        if (not self.at_end() and not self.failed()) {
            self.report(ErrorCode.ExpectedToken, "trailing content after the expression");
        }
        return root;
    }

    // --- Statements ------------------------------------------------------

    fn parse_statement(ref self) -> int {
        if (self.at(TokenKind.Semicolon)) {
            Discard(self.advance());
            return self.tree.add(Node({.kind = NodeKind.Empty}));
        }
        if (self.at(TokenKind.KeywordLet)) {
            return self.parse_let();
        }
        if (self.at(TokenKind.KeywordWhile)) {
            return self.parse_loop();
        }
        if (self.at(TokenKind.KeywordReturn)) {
            return self.parse_return();
        }
        if (self.at(TokenKind.LBrace)) {
            return self.parse_block();
        }
        let value = self.parse_expression(PRECEDENCE_NONE);
        Discard(self.eat(TokenKind.Semicolon));
        return value;
    }

    // `let name = expression ;`
    fn parse_let(ref self) -> int {
        let keyword = self.advance();
        if (not self.at(TokenKind.Identifier)) {
            self.report(ErrorCode.ExpectedIdentifier, "a binding needs a name");
            return NO_NODE;
        }
        let name = self.advance();
        var binding = Node({.kind = NodeKind.Let, .text = name.text});
        binding.span.begin = keyword.span.begin;
        if (self.eat(TokenKind.Equal)) {
            binding.lhs = self.parse_expression(PRECEDENCE_NONE);
        } else {
            binding.lhs = self.tree.add_null();
        }
        Discard(self.eat(TokenKind.Semicolon));
        binding.span.end = self.lookahead.span.begin;
        return self.tree.add(move(binding));
    }

    // `while condition { body }`
    fn parse_loop(ref self) -> int {
        let keyword = self.advance();
        var node = Node({.kind = NodeKind.Loop});
        node.span.begin = keyword.span.begin;
        node.lhs = self.parse_expression(PRECEDENCE_NONE);
        if (self.at(TokenKind.LBrace)) {
            node.rhs = self.parse_block();
        } else {
            node.rhs = self.parse_statement();
        }
        node.span.end = self.lookahead.span.begin;
        return self.tree.add(move(node));
    }

    fn parse_return(ref self) -> int {
        let keyword = self.advance();
        var node = Node({.kind = NodeKind.Return});
        node.span.begin = keyword.span.begin;
        if (self.at(TokenKind.Semicolon)) {
            node.lhs = self.tree.add_null();
        } else {
            node.lhs = self.parse_expression(PRECEDENCE_NONE);
        }
        Discard(self.eat(TokenKind.Semicolon));
        return self.tree.add(move(node));
    }

    // `{ statement* }`
    fn parse_block(ref self) -> int {
        if (not self.enter()) {
            return NO_NODE;
        }
        let open = self.advance();
        var block = Node({.kind = NodeKind.Block});
        block.span.begin = open.span.begin;
        while (not self.at(TokenKind.RBrace) and not self.at_end() and not self.failed()) {
            let statement = self.parse_statement();
            if (statement == NO_NODE) {
                self.recover();
            } else {
                block.parts.push_back(statement);
            }
        }
        if (not self.expect(TokenKind.RBrace)) {
            self.leave();
            return NO_NODE;
        }
        block.span.end = self.lookahead.span.begin;
        self.leave();
        return self.tree.add(move(block));
    }

    // --- The climbing loop -----------------------------------------------
    //
    // This is the heart of the parser. Everything else in this struct exists
    // to be called from here or to call back into here.
    fn parse_expression(ref self, minimum: int) -> int {
        if (not self.enter()) {
            return NO_NODE;
        }
        var left = self.parse_prefix();
        if (left == NO_NODE) {
            self.leave();
            return NO_NODE;
        }

        while (true) {
            if (self.failed()) {
                break;
            }
            let kind = self.peek_kind();

            // Postfix binds tighter than any infix operator, so it is tried
            // first and it never consults `minimum`.
            let postfix = postfix_precedence(kind);
            if (postfix != PRECEDENCE_NONE and postfix >= minimum) {
                let extended = self.parse_postfix(left);
                if (extended == NO_NODE) {
                    break;
                }
                if (extended != left) {
                    left = extended;
                    continue;
                }
            }

            let power = infix_precedence(kind);
            if (power == PRECEDENCE_NONE) {
                break;
            }
            if (power < minimum) {
                break;
            }

            left = self.parse_infix(left, power);
            if (left == NO_NODE) {
                break;
            }
        }

        self.leave();
        return left;
    }

    // --- Prefix rules ----------------------------------------------------
    //
    // One arm per token kind that may begin an expression.
    fn parse_prefix(ref self) -> int {
        let kind = self.peek_kind();

        if (kind == TokenKind.Number) {
            let token = self.advance();
            var literal = Node({.kind = NodeKind.NumberLiteral});
            literal.mantissa = token.mantissa;
            literal.exponent = token.exponent;
            literal.text = token.text;
            literal.span = token.span;
            return self.tree.add(move(literal));
        }
        if (kind == TokenKind.Text) {
            let token = self.advance();
            var literal = Node({.kind = NodeKind.TextLiteral, .text = token.text});
            literal.span = token.span;
            return self.tree.add(move(literal));
        }
        if (kind == TokenKind.KeywordTrue) {
            let token = self.advance();
            var literal = Node({.kind = NodeKind.BoolLiteral, .flag = true});
            literal.span = token.span;
            return self.tree.add(move(literal));
        }
        if (kind == TokenKind.KeywordFalse) {
            let token = self.advance();
            var literal = Node({.kind = NodeKind.BoolLiteral, .flag = false});
            literal.span = token.span;
            return self.tree.add(move(literal));
        }
        if (kind == TokenKind.KeywordNull) {
            let token = self.advance();
            var literal = Node({.kind = NodeKind.NullLiteral});
            literal.span = token.span;
            return self.tree.add(move(literal));
        }
        if (kind == TokenKind.Identifier) {
            let token = self.advance();
            var name = Node({.kind = NodeKind.Variable, .text = token.text});
            name.span = token.span;
            return self.tree.add(move(name));
        }
        if (kind == TokenKind.LParen) {
            return self.parse_group();
        }
        if (kind == TokenKind.LBracket) {
            return self.parse_list();
        }
        if (kind == TokenKind.KeywordIf) {
            return self.parse_if();
        }
        if (prefix_precedence(kind) != PRECEDENCE_NONE) {
            return self.parse_unary();
        }

        var detail: string = "found ";
        detail.append(token_name(kind));
        detail.append(" where an operand was required");
        self.report(ErrorCode.ExpectedExpression, detail);
        return NO_NODE;
    }

    fn parse_unary(ref self) -> int {
        let token = self.advance();
        var node = Node({.kind = NodeKind.Unary, .op = token.kind});
        node.span.begin = token.span.begin;
        node.lhs = self.parse_expression(prefix_precedence(token.kind));
        if (node.lhs == NO_NODE) {
            return NO_NODE;
        }
        node.span.end = self.lookahead.span.begin;
        return self.tree.add(move(node));
    }

    // A parenthesised expression carries no node of its own; the printer
    // reinserts the parentheses from the precedence table instead, which is
    // what makes the round trip minimal rather than literal.
    fn parse_group(ref self) -> int {
        Discard(self.advance());
        let inner = self.parse_expression(PRECEDENCE_NONE);
        if (inner == NO_NODE) {
            return NO_NODE;
        }
        if (not self.at(TokenKind.RParen)) {
            self.report(ErrorCode.UnbalancedParen, "the group opened earlier is never closed");
            return NO_NODE;
        }
        Discard(self.advance());
        return inner;
    }

    // `[ a, b, c ]` builds a Call to the builtin list constructor, which
    // keeps the evaluator from needing a separate node kind.
    fn parse_list(ref self) -> int {
        let open = self.advance();
        var call = Node({.kind = NodeKind.Call});
        call.span.begin = open.span.begin;
        call.lhs = self.tree.add_variable("list");
        if (not self.at(TokenKind.RBracket)) {
            while (true) {
                let element = self.parse_expression(PRECEDENCE_ASSIGN);
                if (element == NO_NODE) {
                    return NO_NODE;
                }
                call.parts.push_back(element);
                if (not self.eat(TokenKind.Comma)) {
                    break;
                }
                if (self.at(TokenKind.RBracket)) {
                    break;
                }
            }
        }
        if (not self.expect(TokenKind.RBracket)) {
            return NO_NODE;
        }
        call.span.end = self.lookahead.span.begin;
        return self.tree.add(move(call));
    }

    // The keyword form of a conditional: `if c then a else b`. The operator
    // form is handled by `parse_infix` when it sees a question mark.
    fn parse_if(ref self) -> int {
        let keyword = self.advance();
        var node = Node({.kind = NodeKind.Conditional});
        node.span.begin = keyword.span.begin;
        node.lhs = self.parse_expression(PRECEDENCE_CONDITIONAL);
        if (node.lhs == NO_NODE) {
            return NO_NODE;
        }
        if (not self.expect(TokenKind.KeywordThen)) {
            return NO_NODE;
        }
        node.rhs = self.parse_expression(PRECEDENCE_CONDITIONAL);
        if (node.rhs == NO_NODE) {
            return NO_NODE;
        }
        if (self.eat(TokenKind.KeywordElse)) {
            node.extra = self.parse_expression(PRECEDENCE_CONDITIONAL);
            if (node.extra == NO_NODE) {
                return NO_NODE;
            }
        } else {
            node.extra = self.tree.add_null();
        }
        node.span.end = self.lookahead.span.begin;
        return self.tree.add(move(node));
    }

    // --- Infix rules -----------------------------------------------------

    fn parse_infix(ref self, left: int, power: int) -> int {
        let token = self.advance();
        let kind = token.kind;

        if (kind == TokenKind.Question) {
            return self.parse_conditional_tail(left);
        }
        if (is_assignment_operator(kind)) {
            return self.parse_assignment_tail(left, kind);
        }

        var next_power = power + 1;
        if (infix_associativity(kind) == Associativity.Right) {
            next_power = power;
        }

        let right = self.parse_expression(next_power);
        if (right == NO_NODE) {
            return NO_NODE;
        }
        var node = Node({.kind = NodeKind.Binary, .op = kind, .lhs = left, .rhs = right});
        node.span.begin = self.tree.at(left).span.begin;
        node.span.end = self.tree.at(right).span.end;
        return self.tree.add(move(node));
    }

    // The question mark has already been consumed; the branches parse at the
    // conditional level so that a nested conditional groups to the right.
    fn parse_conditional_tail(ref self, condition: int) -> int {
        var node = Node({.kind = NodeKind.Conditional, .lhs = condition});
        node.span.begin = self.tree.at(condition).span.begin;
        node.rhs = self.parse_expression(PRECEDENCE_NONE);
        if (node.rhs == NO_NODE) {
            return NO_NODE;
        }
        if (not self.expect(TokenKind.Colon)) {
            return NO_NODE;
        }
        node.extra = self.parse_expression(PRECEDENCE_CONDITIONAL);
        if (node.extra == NO_NODE) {
            return NO_NODE;
        }
        node.flag = true;
        node.span.end = self.lookahead.span.begin;
        return self.tree.add(move(node));
    }

    fn parse_assignment_tail(ref self, target: int, op: TokenKind) -> int {
        if (not self.tree.at(target).is_assignable()) {
            var detail: string = "cannot assign to a ";
            detail.append(node_kind_name(self.tree.kind_of(target)));
            detail.append(" expression");
            self.report(ErrorCode.NotAssignable, detail);
            return NO_NODE;
        }
        let value = self.parse_expression(PRECEDENCE_ASSIGN);
        if (value == NO_NODE) {
            return NO_NODE;
        }
        var node = Node({.kind = NodeKind.Assign, .op = op, .lhs = target, .rhs = value});
        node.span.begin = self.tree.at(target).span.begin;
        node.span.end = self.tree.at(value).span.end;
        return self.tree.add(move(node));
    }

    // --- Postfix rules ---------------------------------------------------
    //
    // Returns `left` unchanged when the next token turns out not to start a
    // suffix after all, which the climbing loop reads as "stop trying".
    fn parse_postfix(ref self, left: int) -> int {
        let kind = self.peek_kind();
        if (kind == TokenKind.LParen) {
            return self.parse_call(left);
        }
        if (kind == TokenKind.LBracket) {
            return self.parse_index(left);
        }
        if (kind == TokenKind.Dot) {
            return self.parse_member(left);
        }
        if (kind == TokenKind.PlusPlus or kind == TokenKind.MinusMinus) {
            let token = self.advance();
            var node = Node({.kind = NodeKind.Postfix, .op = token.kind, .lhs = left});
            node.span.begin = self.tree.at(left).span.begin;
            node.span.end = token.span.end;
            return self.tree.add(move(node));
        }
        if (kind == TokenKind.Bang) {
            // Postfix `!` is factorial, but only after something that can
            // actually produce a number.
            if (self.tree.kind_of(left) == NodeKind.NumberLiteral or self.tree.kind_of(left) == NodeKind.Variable) {
                let token = self.advance();
                var node = Node({.kind = NodeKind.Postfix, .op = token.kind, .lhs = left});
                node.span.begin = self.tree.at(left).span.begin;
                node.span.end = token.span.end;
                return self.tree.add(move(node));
            }
        }
        return left;
    }

    // `callee ( argument, name: argument )`
    fn parse_call(ref self, callee: int) -> int {
        let open = self.advance();
        var node = Node({.kind = NodeKind.Call, .lhs = callee});
        node.span.begin = self.tree.at(callee).span.begin;
        if (not self.at(TokenKind.RParen)) {
            while (true) {
                if (node.parts.size() >= MAX_ARGUMENTS) {
                    self.report(ErrorCode.TooManyArguments, "argument limit reached");
                    return NO_NODE;
                }
                let argument = self.parse_argument();
                if (argument == NO_NODE) {
                    return NO_NODE;
                }
                node.parts.push_back(argument);
                if (not self.eat(TokenKind.Comma)) {
                    break;
                }
            }
        }
        if (not self.expect(TokenKind.RParen)) {
            return NO_NODE;
        }
        node.span.end = self.lookahead.span.begin;
        Discard(open);
        return self.tree.add(move(node));
    }

    // An argument is either a bare expression or `name: expression`. The
    // label is stored on a Let node wrapping the value, which the evaluator
    // unwraps when it binds parameters.
    fn parse_argument(ref self) -> int {
        if (self.at(TokenKind.Identifier)) {
            let saved = self.lookahead;
            let name = self.advance();
            if (self.eat(TokenKind.Colon)) {
                var labelled = Node({.kind = NodeKind.Let, .text = name.text, .flag = true});
                labelled.span.begin = name.span.begin;
                labelled.lhs = self.parse_expression(PRECEDENCE_ASSIGN);
                if (labelled.lhs == NO_NODE) {
                    return NO_NODE;
                }
                labelled.span.end = self.lookahead.span.begin;
                return self.tree.add(move(labelled));
            }
            // Not a label: rebuild the variable the lookahead already ate and
            // keep parsing from there.
            var variable = Node({.kind = NodeKind.Variable, .text = name.text});
            variable.span = saved.span;
            let base_ = self.tree.add(move(variable));
            return self.continue_expression(base_, PRECEDENCE_ASSIGN);
        }
        return self.parse_expression(PRECEDENCE_ASSIGN);
    }

    // Resume the climbing loop with an operand that has already been built.
    // This is what lets `parse_argument` back out of a mistaken label.
    fn continue_expression(ref self, left: int, minimum: int) -> int {
        var current = left;
        while (true) {
            if (self.failed()) {
                break;
            }
            let kind = self.peek_kind();
            let postfix = postfix_precedence(kind);
            if (postfix != PRECEDENCE_NONE) {
                let extended = self.parse_postfix(current);
                if (extended == NO_NODE) {
                    return NO_NODE;
                }
                if (extended != current) {
                    current = extended;
                    continue;
                }
            }
            let power = infix_precedence(kind);
            if (power == PRECEDENCE_NONE or power < minimum) {
                break;
            }
            current = self.parse_infix(current, power);
            if (current == NO_NODE) {
                return NO_NODE;
            }
        }
        return current;
    }

    fn parse_index(ref self, target: int) -> int {
        Discard(self.advance());
        var node = Node({.kind = NodeKind.Index, .lhs = target});
        node.span.begin = self.tree.at(target).span.begin;
        node.rhs = self.parse_expression(PRECEDENCE_NONE);
        if (node.rhs == NO_NODE) {
            return NO_NODE;
        }
        if (not self.at(TokenKind.RBracket)) {
            self.report(ErrorCode.UnbalancedBracket, "the subscript opened earlier is never closed");
            return NO_NODE;
        }
        Discard(self.advance());
        node.span.end = self.lookahead.span.begin;
        return self.tree.add(move(node));
    }

    fn parse_member(ref self, target: int) -> int {
        Discard(self.advance());
        if (not self.at(TokenKind.Identifier)) {
            self.report(ErrorCode.ExpectedIdentifier, "a member access needs a field name");
            return NO_NODE;
        }
        let name = self.advance();
        var node = Node({.kind = NodeKind.Member, .lhs = target, .text = name.text});
        node.span.begin = self.tree.at(target).span.begin;
        node.span.end = name.span.end;
        return self.tree.add(move(node));
    }

    fn arena(ref self) -> ref Arena {
        return self.tree;
    }

    var scanner: Tokenizer;
    var lookahead: Token;
    var tree: Arena;
    var problem: Diagnostic;
    var depth: int = 0;
}

// One shot helper: parse a source string and hand back both the arena and
// the root, so callers do not have to keep the parser alive.
class ParseResult {

    fn ok(self) -> bool {
        if (self.root == NO_NODE) {
            return false;
        }
        return self.problem.ok();
    }

    fn describe(self) -> string {
        if (self.ok()) {
            return "parsed";
        }
        return self.problem.describe();
    }

    var tree: Arena;
    var root: int = 0;
    var problem: Diagnostic;
}

fn parse_expression_text(source: string) -> ParseResult {
    var parser: Parser;
    let root = parser.parse_source(source);
    var out: ParseResult;
    out.root = root;
    out.problem = parser.problem;
    out.tree = move(parser.tree);
    return out;
}

fn parse_program_text(source: string) -> ParseResult {
    var parser: Parser;
    let root = parser.parse_program(source);
    var out: ParseResult;
    out.root = root;
    out.problem = parser.problem;
    out.tree = move(parser.tree);
    return out;
}

// --- Scaled decimal arithmetic -------------------------------------------
//
// Every number in this language is a pair (mantissa, exponent) standing for
// mantissa * 10^exponent. Addition has to line the exponents up first;
// multiplication just adds them. Nothing here rounds to nearest, it
// truncates, which is documented behaviour rather than an accident.

fn power_of_ten(n: int) -> int {
    var out: int = 1;
    var left = n;
    while (left > 0) {
        out *= NUMBER_BASE;
        left -= 1;
    }
    return out;
}

fn digit_count(value: int) -> int {
    var left = abs(value);
    var count: int = 0;
    while (left > 0) {
        left /= NUMBER_BASE;
        count += 1;
    }
    if (count == 0) {
        return 1;
    }
    return count;
}

// Strip trailing zeros from the mantissa so that 1500 * 10^-2 and 15 * 10^0
// compare and print the same way.
class Decimal {

    fn is_zero(self) -> bool {
        return self.mantissa == 0;
    }

    fn is_negative(self) -> bool {
        return self.mantissa < 0;
    }

    fn is_integral(self) -> bool {
        if (self.exponent >= 0) {
            return true;
        }
        var copy = *self;
        copy.normalize();
        return copy.exponent >= 0;
    }

    fn normalize(ref self) {
        if (self.mantissa == 0) {
            self.exponent = 0;
            return;
        }
        while (self.exponent < 0 and self.mantissa % NUMBER_BASE == 0) {
            self.mantissa /= NUMBER_BASE;
            self.exponent += 1;
        }
    }

    fn scaled_to(self, target: int) -> int {
        if (target >= self.exponent) {
            return self.mantissa / power_of_ten(target - self.exponent);
        }
        return self.mantissa * power_of_ten(self.exponent - target);
    }

    fn to_int(self) -> int {
        return self.scaled_to(0);
    }

    // The printed form. Integral values print without a point; fractional
    // ones get exactly as many digits as the exponent calls for.
    fn to_text(self) -> string {
        var copy = *self;
        copy.normalize();
        if (copy.exponent >= 0) {
            var whole = to_string(copy.mantissa);
            var zeros = copy.exponent;
            while (zeros > 0) {
                whole.push_back(CHAR_ZERO);
                zeros -= 1;
            }
            return whole;
        }
        var out: string = "";
        if (copy.mantissa < 0) {
            out.push_back(CHAR_MINUS);
        }
        let magnitude = abs(copy.mantissa);
        let places = -copy.exponent;
        let divisor = power_of_ten(places);
        out.append(to_string(magnitude / divisor));
        out.push_back(CHAR_DOT);
        var fraction = to_string(magnitude % divisor);
        var pad = places - fraction.size();
        while (pad > 0) {
            out.push_back(CHAR_ZERO);
            pad -= 1;
        }
        out.append(fraction);
        return out;
    }

    var mantissa: int = 0;
    var exponent: int = 0;
}

fn make_decimal(mantissa: int, exponent: int) -> Decimal {
    var out = Decimal({.mantissa = mantissa, .exponent = exponent});
    out.normalize();
    return out;
}

fn decimal_from_int(value: int) -> Decimal {
    return Decimal({.mantissa = value, .exponent = 0});
}

// The common exponent two operands have to be brought to before they can be
// added or compared. Always the smaller of the two, so no digits are lost.
fn common_exponent(a: Decimal, b: Decimal) -> int {
    if (a.exponent < b.exponent) {
        return a.exponent;
    }
    return b.exponent;
}

fn decimal_add(a: Decimal, b: Decimal) -> Decimal {
    let e = common_exponent(a, b);
    return make_decimal(a.scaled_to(e) + b.scaled_to(e), e);
}

fn decimal_subtract(a: Decimal, b: Decimal) -> Decimal {
    let e = common_exponent(a, b);
    return make_decimal(a.scaled_to(e) - b.scaled_to(e), e);
}

fn decimal_multiply(a: Decimal, b: Decimal) -> Decimal {
    return make_decimal(a.mantissa * b.mantissa, a.exponent + b.exponent);
}

// Division keeps DIVISION_DIGITS extra digits of the quotient and lowers the
// exponent to match, which is how 1 / 3 comes out as 0.333333.
let DIVISION_DIGITS: int = 6;

fn decimal_divide(a: Decimal, b: Decimal) -> Decimal {
    if (b.mantissa == 0) {
        return Decimal({.mantissa = 0, .exponent = 0});
    }
    let lifted = a.mantissa * power_of_ten(DIVISION_DIGITS);
    let exponent = a.exponent - b.exponent - DIVISION_DIGITS;
    return make_decimal(lifted / b.mantissa, exponent);
}

fn decimal_remainder(a: Decimal, b: Decimal) -> Decimal {
    if (b.mantissa == 0) {
        return Decimal({.mantissa = 0, .exponent = 0});
    }
    let e = common_exponent(a, b);
    return make_decimal(a.scaled_to(e) % b.scaled_to(e), e);
}

fn decimal_negate(a: Decimal) -> Decimal {
    return make_decimal(-a.mantissa, a.exponent);
}

// Only integral exponents are supported, which is all the language exposes.
fn decimal_power(base_: Decimal, exponent: Decimal) -> Decimal {
    var count = exponent.to_int();
    if (count < 0) {
        let one = decimal_from_int(1);
        var positive = *exponent;
        positive.mantissa = -positive.mantissa;
        return decimal_divide(one, decimal_power(base_, positive));
    }
    var out = decimal_from_int(1);
    while (count > 0) {
        out = decimal_multiply(out, *base_);
        count -= 1;
    }
    return out;
}

// Returns -1, 0 or 1.
fn decimal_compare(a: Decimal, b: Decimal) -> int {
    let e = common_exponent(a, b);
    let left = a.scaled_to(e);
    let right = b.scaled_to(e);
    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

fn decimal_equal(a: Decimal, b: Decimal) -> bool {
    return decimal_compare(a, b) == 0;
}

// --- Values --------------------------------------------------------------
//
// A tagged union. charge has no variant type yet, so each payload gets its
// own field and `kind` says which one is live.
class Value {

    fn is_nil(self) -> bool {
        return self.kind == ValueKind.Nil;
    }

    fn is_number(self) -> bool {
        return self.kind == ValueKind.Number;
    }

    fn is_text(self) -> bool {
        return self.kind == ValueKind.Text;
    }

    fn is_bool(self) -> bool {
        return self.kind == ValueKind.Bool;
    }

    fn is_list(self) -> bool {
        return self.kind == ValueKind.List;
    }

    fn is_failure(self) -> bool {
        return self.kind == ValueKind.Failure;
    }

    // Every value has a truth value; only nil, false, zero, the empty string
    // and the empty list are falsy.
    fn truthy(self) -> bool {
        if (self.kind == ValueKind.Nil) {
            return false;
        }
        if (self.kind == ValueKind.Bool) {
            return self.flag;
        }
        if (self.kind == ValueKind.Number) {
            return not self.number.is_zero();
        }
        if (self.kind == ValueKind.Text) {
            return self.text.size() > 0;
        }
        if (self.kind == ValueKind.List) {
            return self.items.size() > 0;
        }
        if (self.kind == ValueKind.Failure) {
            return false;
        }
        return true;
    }

    fn kind_name(self) -> string {
        if (self.kind == ValueKind.Nil) { return "null"; }
        if (self.kind == ValueKind.Bool) { return "bool"; }
        if (self.kind == ValueKind.Number) { return "number"; }
        if (self.kind == ValueKind.Text) { return "string"; }
        if (self.kind == ValueKind.List) { return "list"; }
        if (self.kind == ValueKind.Builtin) { return "builtin"; }
        if (self.kind == ValueKind.Failure) { return "error"; }
        return "unknown";
    }

    fn size(self) -> int {
        if (self.kind == ValueKind.List) {
            return self.items.size();
        }
        if (self.kind == ValueKind.Text) {
            return self.text.size();
        }
        return 0;
    }

    // The display form, used by `print` and by the test harness.
    fn to_text(self) -> string {
        if (self.kind == ValueKind.Nil) {
            return WORD_NULL;
        }
        if (self.kind == ValueKind.Bool) {
            if (self.flag) {
                return WORD_TRUE;
            }
            return WORD_FALSE;
        }
        if (self.kind == ValueKind.Number) {
            return self.number.to_text();
        }
        if (self.kind == ValueKind.Text) {
            return self.text;
        }
        if (self.kind == ValueKind.Builtin) {
            var out: string = "<builtin ";
            out.append(self.text);
            out.append(">");
            return out;
        }
        if (self.kind == ValueKind.Failure) {
            var out: string = "<error: ";
            out.append(self.text);
            out.append(">");
            return out;
        }
        var out: string = "[";
        var first = true;
        for (element in self.items) {
            if (not first) {
                out.append(TEXT_COMMA_SPACE);
            }
            out.append(element.to_text());
            first = false;
        }
        out.append("]");
        return out;
    }

    var kind: ValueKind = ValueKind.Nil;
    var number: Decimal;
    var text: string;
    var flag: bool = false;
    var items: vector(Value);
    // For builtins: the arity the dispatcher checks before calling.
    var arity: int = 0;
}

fn nil_value() -> Value {
    return Value({.kind = ValueKind.Nil});
}

fn bool_value(flag: bool) -> Value {
    return Value({.kind = ValueKind.Bool, .flag = flag});
}

fn number_value(number: Decimal) -> Value {
    return Value({.kind = ValueKind.Number, .number = number});
}

fn int_value(n: int) -> Value {
    return number_value(decimal_from_int(n));
}

fn text_value(text: string) -> Value {
    return Value({.kind = ValueKind.Text, .text = *text});
}

fn list_value(items: vector(Value)) -> Value {
    return Value({.kind = ValueKind.List, .items = move(items)});
}

fn failure_value(detail: string) -> Value {
    return Value({.kind = ValueKind.Failure, .text = *detail});
}

// Structural equality across kinds. Numbers compare by value, so 1.50 equals
// 1.5, and lists compare element by element.
fn values_equal(a: Value, b: Value) -> bool {
    if (a.kind != b.kind) {
        return false;
    }
    if (a.kind == ValueKind.Nil) {
        return true;
    }
    if (a.kind == ValueKind.Bool) {
        return a.flag == b.flag;
    }
    if (a.kind == ValueKind.Number) {
        return decimal_equal(a.number, b.number);
    }
    if (a.kind == ValueKind.Text) {
        return a.text == b.text;
    }
    if (a.kind == ValueKind.List) {
        if (a.items.size() != b.items.size()) {
            return false;
        }
        for (i in range(0, a.items.size())) {
            if (not values_equal(a.items[i], b.items[i])) {
                return false;
            }
        }
        return true;
    }
    return a.text == b.text;
}

// Ordering, defined only for numbers and strings.
fn compare_values(a: Value, b: Value) -> int {
    if (a.kind == ValueKind.Number and b.kind == ValueKind.Number) {
        return decimal_compare(a.number, b.number);
    }
    if (a.kind == ValueKind.Text and b.kind == ValueKind.Text) {
        return a.text.compare(b.text);
    }
    if (values_equal(a, b)) {
        return 0;
    }
    return -1;
}

// --- Scopes and environments ---------------------------------------------
//
// Lexical scoping with a flat vector of frames. A lookup walks outwards from
// the innermost frame, so an inner binding shadows an outer one, and a
// frame boundary is just an index into the same two parallel vectors.
class Scope {

    fn size(self) -> int {
        return self.names.size();
    }

    fn index_of(self, name: string) -> int {
        var i = self.names.size() - 1;
        while (i >= 0) {
            if (self.names[i] == *name) {
                return i;
            }
            i -= 1;
        }
        return -1;
    }

    fn contains(self, name: string) -> bool {
        return self.index_of(name) >= 0;
    }

    fn define(ref self, name: string, value: Value) {
        let existing = self.index_of(name);
        if (existing >= 0) {
            self.values[existing] = move(value);
            return;
        }
        self.names.push_back(*name);
        self.values.push_back(move(value));
    }

    fn at(ref self, index: int) -> ref Value {
        return self.values[index];
    }

    fn clear(ref self) {
        self.names.clear();
        self.values.clear();
    }

    var names: vector(string);
    var values: vector(Value);
    // Purely informational, but it makes a dumped environment readable.
    var label: string;
}

class Environment {

    fn depth(self) -> int {
        return self.frames.size();
    }

    fn global(ref self) -> ref Scope {
        return self.frames[0];
    }

    fn innermost(ref self) -> ref Scope {
        return self.frames[self.frames.size() - 1];
    }

    fn push_scope(ref self, label: string) {
        self.frames.push_back(Scope({.label = *label}));
    }

    fn pop_scope(ref self) {
        if (self.frames.size() > 1) {
            self.frames.pop_back();
        }
    }

    fn define(ref self, name: string, value: Value) {
        let ref frame: auto = self.frames[self.frames.size() - 1];
        frame.define(name, move(value));
    }

    fn define_global(ref self, name: string, value: Value) {
        let ref frame: auto = self.frames[0];
        frame.define(name, move(value));
    }

    fn bound(self, name: string) -> bool {
        var i = self.frames.size() - 1;
        while (i >= 0) {
            if (self.frames[i].contains(name)) {
                return true;
            }
            i -= 1;
        }
        return false;
    }

    // Look a name up from the innermost frame outwards. Returns the missing
    // sentinel when nothing matches, which the evaluator turns into an
    // UnknownName failure.
    fn lookup(ref self, name: string) -> ref Value {
        var i = self.frames.size() - 1;
        while (i >= 0) {
            let ref frame: auto = self.frames[i];
            let slot = frame.index_of(name);
            if (slot >= 0) {
                return frame.at(slot);
            }
            i -= 1;
        }
        return self.missing;
    }

    // Assignment writes through to the frame the name was found in, rather
    // than shadowing it in the current one.
    fn assign(ref self, name: string, value: Value) -> bool {
        var i = self.frames.size() - 1;
        while (i >= 0) {
            let ref frame: auto = self.frames[i];
            let slot = frame.index_of(name);
            if (slot >= 0) {
                frame.values[slot] = move(value);
                return true;
            }
            i -= 1;
        }
        return false;
    }

    fn describe(self) -> string {
        var out: string = "";
        for (frame in self.frames) {
            out.append(frame.label);
            out.append(" {");
            for (i in range(0, frame.names.size())) {
                if (i > 0) {
                    out.append(TEXT_COMMA_SPACE);
                }
                out.append(frame.names[i]);
            }
            out.append("}\n");
        }
        return out;
    }

    var frames: vector(Scope);
    var missing: Value;
}

fn make_environment() -> Environment {
    var env: Environment;
    env.frames.push_back(Scope({.label = "global"}));
    return env;
}

// --- Builtins ------------------------------------------------------------
//
// Builtins are dispatched by name from one cascade. Registering a name in
// the environment only records its arity; the body lives in `call_builtin`.

fn builtin_arity(name: string) -> int {
    if (*name == "abs") { return 1; }
    if (*name == "min") { return 2; }
    if (*name == "max") { return 2; }
    if (*name == "floor") { return 1; }
    if (*name == "ceil") { return 1; }
    if (*name == "round") { return 1; }
    if (*name == "sqrt") { return 1; }
    if (*name == "len") { return 1; }
    if (*name == "upper") { return 1; }
    if (*name == "lower") { return 1; }
    if (*name == "trim") { return 1; }
    if (*name == "concat") { return 2; }
    if (*name == "at") { return 2; }
    if (*name == "slice") { return 3; }
    if (*name == "push") { return 2; }
    if (*name == "sum") { return 1; }
    if (*name == "type") { return 1; }
    if (*name == "number") { return 1; }
    if (*name == "string") { return 1; }
    if (*name == "print") { return 1; }
    // A negative arity means "any number of arguments".
    if (*name == "list") { return -1; }
    return -2;
}

fn is_builtin(name: string) -> bool {
    return builtin_arity(name) != -2;
}

fn builtin_names() -> vector(string) {
    var names: vector(string);
    names.push_back("abs");
    names.push_back("min");
    names.push_back("max");
    names.push_back("floor");
    names.push_back("ceil");
    names.push_back("round");
    names.push_back("sqrt");
    names.push_back("len");
    names.push_back("upper");
    names.push_back("lower");
    names.push_back("trim");
    names.push_back("concat");
    names.push_back("at");
    names.push_back("slice");
    names.push_back("push");
    names.push_back("sum");
    names.push_back("type");
    names.push_back("number");
    names.push_back("string");
    names.push_back("print");
    names.push_back("list");
    return names;
}

// Builtins the folder is allowed to evaluate at compile time. Anything that
// prints or mutates is deliberately absent.
fn builtin_is_pure(name: string) -> bool {
    if (*name == "print") {
        return false;
    }
    if (*name == "push") {
        return false;
    }
    return is_builtin(name);
}

// Integer square root by bisection, on the scaled representation.
fn decimal_sqrt(value: Decimal) -> Decimal {
    let target = value.scaled_to(-2);
    if (target <= 0) {
        return decimal_from_int(0);
    }
    var low: int = 0;
    var high = target;
    while (low < high) {
        let middle = (low + high + 1) / 2;
        if (middle * middle <= target * 100) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return make_decimal(low, -2);
}

fn decimal_floor(value: Decimal) -> Decimal {
    let whole = value.scaled_to(0);
    var out = decimal_from_int(whole);
    if (value.is_negative() and decimal_compare(out, *value) != 0) {
        out = decimal_from_int(whole - 1);
    }
    return out;
}

fn decimal_ceil(value: Decimal) -> Decimal {
    let floored = decimal_floor(value);
    if (decimal_equal(floored, *value)) {
        return floored;
    }
    return decimal_add(floored, decimal_from_int(1));
}

fn decimal_round(value: Decimal) -> Decimal {
    let half = make_decimal(5, -1);
    return decimal_floor(decimal_add(*value, half));
}

fn text_upper(text: string) -> string {
    var out: string = "";
    for (i in range(0, text.size())) {
        out.push_back(to_upper(text.at(i)));
    }
    return out;
}

fn text_lower(text: string) -> string {
    var out: string = "";
    for (i in range(0, text.size())) {
        out.push_back(to_lower(text.at(i)));
    }
    return out;
}

fn text_trim(text: string) -> string {
    var begin: int = 0;
    var end = text.size();
    while (begin < end and is_space(text.at(begin))) {
        begin += 1;
    }
    while (end > begin and is_space(text.at(end - 1))) {
        end -= 1;
    }
    return text.substr(begin, end - begin);
}

// --- The interpreter -----------------------------------------------------
//
// A straight tree walk. `evaluate` dispatches on the node kind and every arm
// calls back into `evaluate`, so this family is as mutually recursive as the
// parser above it.
class Interpreter {

    fn fail(ref self, code: ErrorCode, detail: string) -> Value {
        if (self.problem.ok()) {
            self.problem.code = code;
            self.problem.detail = *detail;
        }
        var message = error_text(code);
        message.append(": ");
        message.append(*detail);
        return failure_value(message);
    }

    fn node_kind(self, index: int) -> NodeKind {
        return self.tree.kind_of(index);
    }

    // --- Dispatch --------------------------------------------------------

    fn evaluate(ref self, index: int) -> Value {
        if (not self.tree.valid(index)) {
            return nil_value();
        }
        self.steps += 1;
        let kind = self.tree.kind_of(index);

        if (kind == NodeKind.NumberLiteral) {
            return number_value(make_decimal(self.tree.at(index).mantissa, self.tree.at(index).exponent));
        }
        if (kind == NodeKind.TextLiteral) {
            return text_value(self.tree.at(index).text);
        }
        if (kind == NodeKind.BoolLiteral) {
            return bool_value(self.tree.at(index).flag);
        }
        if (kind == NodeKind.NullLiteral) {
            return nil_value();
        }
        if (kind == NodeKind.Empty) {
            return nil_value();
        }
        if (kind == NodeKind.Variable) {
            return self.evaluate_variable(index);
        }
        if (kind == NodeKind.Unary) {
            return self.evaluate_unary(index);
        }
        if (kind == NodeKind.Postfix) {
            return self.evaluate_postfix(index);
        }
        if (kind == NodeKind.Binary) {
            return self.evaluate_binary(index);
        }
        if (kind == NodeKind.Assign) {
            return self.evaluate_assign(index);
        }
        if (kind == NodeKind.Conditional) {
            return self.evaluate_conditional(index);
        }
        if (kind == NodeKind.Call) {
            return self.evaluate_call(index);
        }
        if (kind == NodeKind.Index) {
            return self.evaluate_index(index);
        }
        if (kind == NodeKind.Member) {
            return self.evaluate_member(index);
        }
        if (kind == NodeKind.Let) {
            return self.evaluate_let(index);
        }
        if (kind == NodeKind.Block) {
            return self.evaluate_block(index);
        }
        if (kind == NodeKind.Loop) {
            return self.evaluate_loop(index);
        }
        if (kind == NodeKind.Return) {
            var out = self.evaluate(self.tree.at(index).lhs);
            self.returning = true;
            return out;
        }
        return self.fail(ErrorCode.ExpectedExpression, "cannot evaluate this node");
    }

    fn evaluate_variable(ref self, index: int) -> Value {
        let name = self.tree.at(index).text;
        if (self.environment.bound(name)) {
            return self.environment.lookup(name);
        }
        if (is_builtin(name)) {
            return Value({.kind = ValueKind.Builtin, .text = name, .arity = builtin_arity(name)});
        }
        var detail: string = "no binding named '";
        detail.append(name);
        detail.append("'");
        return self.fail(ErrorCode.UnknownName, detail);
    }

    // --- Operators -------------------------------------------------------

    fn evaluate_unary(ref self, index: int) -> Value {
        let op = self.tree.at(index).op;
        let operand = self.evaluate(self.tree.at(index).lhs);
        if (operand.is_failure()) {
            return operand;
        }

        if (op == TokenKind.Minus) {
            if (not operand.is_number()) {
                return self.fail(ErrorCode.TypeMismatch, "unary minus needs a number");
            }
            return number_value(decimal_negate(operand.number));
        }
        if (op == TokenKind.Plus) {
            if (not operand.is_number()) {
                return self.fail(ErrorCode.TypeMismatch, "unary plus needs a number");
            }
            return operand;
        }
        if (op == TokenKind.Bang or op == TokenKind.KeywordNot) {
            return bool_value(not operand.truthy());
        }
        if (op == TokenKind.Tilde) {
            if (not operand.is_number()) {
                return self.fail(ErrorCode.TypeMismatch, "bitwise complement needs a number");
            }
            return int_value(^operand.number.to_int());
        }
        if (op == TokenKind.PlusPlus or op == TokenKind.MinusMinus) {
            return self.apply_step(self.tree.at(index).lhs, op, false);
        }
        return self.fail(ErrorCode.TypeMismatch, "unknown prefix operator");
    }

    fn evaluate_postfix(ref self, index: int) -> Value {
        let op = self.tree.at(index).op;
        if (op == TokenKind.PlusPlus or op == TokenKind.MinusMinus) {
            return self.apply_step(self.tree.at(index).lhs, op, true);
        }
        if (op == TokenKind.Bang) {
            let operand = self.evaluate(self.tree.at(index).lhs);
            if (not operand.is_number()) {
                return self.fail(ErrorCode.TypeMismatch, "factorial needs a number");
            }
            var count = operand.number.to_int();
            var out: int = 1;
            while (count > 1) {
                out *= count;
                count -= 1;
            }
            return int_value(out);
        }
        return self.fail(ErrorCode.TypeMismatch, "unknown postfix operator");
    }

    // Shared by both the prefix and the postfix increment. `after` decides
    // which of the two values the expression itself produces.
    fn apply_step(ref self, target: int, op: TokenKind, after: bool) -> Value {
        if (self.tree.kind_of(target) != NodeKind.Variable) {
            return self.fail(ErrorCode.NotAssignable, "increment needs a variable");
        }
        let name = self.tree.at(target).text;
        let current = self.environment.lookup(name);
        if (not current.is_number()) {
            return self.fail(ErrorCode.TypeMismatch, "increment needs a number");
        }
        var stepped = current.number;
        if (op == TokenKind.PlusPlus) {
            stepped = decimal_add(stepped, decimal_from_int(1));
        } else {
            stepped = decimal_subtract(stepped, decimal_from_int(1));
        }
        Discard(self.environment.assign(name, number_value(stepped)));
        if (after) {
            return number_value(current.number);
        }
        return number_value(stepped);
    }

    fn evaluate_binary(ref self, index: int) -> Value {
        let op = self.tree.at(index).op;

        // The short circuit operators must not evaluate their right side.
        if (is_short_circuit(op)) {
            let left = self.evaluate(self.tree.at(index).lhs);
            if (left.is_failure()) {
                return left;
            }
            if (op == TokenKind.AmpAmp or op == TokenKind.KeywordAnd) {
                if (not left.truthy()) {
                    return bool_value(false);
                }
                let right = self.evaluate(self.tree.at(index).rhs);
                if (right.is_failure()) {
                    return right;
                }
                return bool_value(right.truthy());
            }
            if (left.truthy()) {
                return bool_value(true);
            }
            let right = self.evaluate(self.tree.at(index).rhs);
            if (right.is_failure()) {
                return right;
            }
            return bool_value(right.truthy());
        }

        let left = self.evaluate(self.tree.at(index).lhs);
        if (left.is_failure()) {
            return left;
        }
        let right = self.evaluate(self.tree.at(index).rhs);
        if (right.is_failure()) {
            return right;
        }
        return self.apply_binary(op, left, right);
    }

    fn apply_binary(ref self, op: TokenKind, left: Value, right: Value) -> Value {
        if (op == TokenKind.EqualEqual) {
            return bool_value(values_equal(left, right));
        }
        if (op == TokenKind.BangEqual) {
            return bool_value(not values_equal(left, right));
        }
        if (is_comparison(op)) {
            let order = compare_values(left, right);
            if (op == TokenKind.Less) { return bool_value(order < 0); }
            if (op == TokenKind.LessEqual) { return bool_value(order <= 0); }
            if (op == TokenKind.Greater) { return bool_value(order > 0); }
            if (op == TokenKind.GreaterEqual) { return bool_value(order >= 0); }
        }

        // `+` is overloaded on strings and lists as concatenation.
        if (op == TokenKind.Plus) {
            if (left.is_text() or right.is_text()) {
                var joined = left.to_text();
                joined.append(right.to_text());
                return text_value(joined);
            }
            if (left.is_list()) {
                var items = left.items;
                if (right.is_list()) {
                    for (element in right.items) {
                        items.push_back(*element);
                    }
                } else {
                    items.push_back(*right);
                }
                return list_value(move(items));
            }
        }

        if (not left.is_number() or not right.is_number()) {
            var detail: string = "operator ";
            detail.append(token_spelling(op));
            detail.append(" does not accept ");
            detail.append(left.kind_name());
            detail.append(" and ");
            detail.append(right.kind_name());
            return self.fail(ErrorCode.TypeMismatch, detail);
        }

        if (op == TokenKind.Plus) {
            return number_value(decimal_add(left.number, right.number));
        }
        if (op == TokenKind.Minus) {
            return number_value(decimal_subtract(left.number, right.number));
        }
        if (op == TokenKind.Star) {
            return number_value(decimal_multiply(left.number, right.number));
        }
        if (op == TokenKind.Slash) {
            if (right.number.is_zero()) {
                return self.fail(ErrorCode.DivideByZero, "the divisor evaluates to zero");
            }
            return number_value(decimal_divide(left.number, right.number));
        }
        if (op == TokenKind.Percent) {
            if (right.number.is_zero()) {
                return self.fail(ErrorCode.DivideByZero, "the modulus evaluates to zero");
            }
            return number_value(decimal_remainder(left.number, right.number));
        }
        if (op == TokenKind.StarStar) {
            return number_value(decimal_power(left.number, right.number));
        }

        // The bitwise family works on the truncated integer value.
        let a = left.number.to_int();
        let b = right.number.to_int();
        if (op == TokenKind.Amp) { return int_value(a & b); }
        if (op == TokenKind.Pipe) { return int_value(a | b); }
        if (op == TokenKind.Caret) { return int_value(a ^ b); }
        if (op == TokenKind.LessLess) { return int_value(a << b); }
        if (op == TokenKind.GreaterGreater) { return int_value(a >> b); }

        var detail: string = "unknown operator ";
        detail.append(token_spelling(op));
        return self.fail(ErrorCode.TypeMismatch, detail);
    }

    fn evaluate_assign(ref self, index: int) -> Value {
        let op = self.tree.at(index).op;
        let target = self.tree.at(index).lhs;
        if (self.tree.kind_of(target) != NodeKind.Variable) {
            return self.fail(ErrorCode.NotAssignable, "only plain names may be assigned to");
        }
        let name = self.tree.at(target).text;
        var value = self.evaluate(self.tree.at(index).rhs);
        if (value.is_failure()) {
            return value;
        }
        if (op != TokenKind.Equal) {
            let base_ = compound_base_operator(op);
            let current = self.environment.lookup(name);
            value = self.apply_binary(base_, current, value);
            if (value.is_failure()) {
                return value;
            }
        }
        if (not self.environment.assign(name, value)) {
            self.environment.define(name, value);
        }
        return value;
    }

    fn evaluate_conditional(ref self, index: int) -> Value {
        let condition = self.evaluate(self.tree.at(index).lhs);
        if (condition.is_failure()) {
            return condition;
        }
        if (condition.truthy()) {
            return self.evaluate(self.tree.at(index).rhs);
        }
        return self.evaluate(self.tree.at(index).extra);
    }

    fn evaluate_let(ref self, index: int) -> Value {
        let value = self.evaluate(self.tree.at(index).lhs);
        if (value.is_failure()) {
            return value;
        }
        self.environment.define(self.tree.at(index).text, value);
        return value;
    }

    fn evaluate_block(ref self, index: int) -> Value {
        self.environment.push_scope("block");
        var last = nil_value();
        for (statement in self.tree.at(index).parts) {
            last = self.evaluate(statement);
            if (last.is_failure()) {
                break;
            }
            if (self.returning) {
                break;
            }
        }
        self.environment.pop_scope();
        return last;
    }

    fn evaluate_loop(ref self, index: int) -> Value {
        var last = nil_value();
        var rounds: int = 0;
        while (true) {
            let condition = self.evaluate(self.tree.at(index).lhs);
            if (condition.is_failure()) {
                return condition;
            }
            if (not condition.truthy()) {
                break;
            }
            last = self.evaluate(self.tree.at(index).rhs);
            if (last.is_failure()) {
                return last;
            }
            if (self.returning) {
                break;
            }
            rounds += 1;
            if (rounds > self.step_limit) {
                return self.fail(ErrorCode.DepthExceeded, "loop ran past the step limit");
            }
        }
        return last;
    }

    fn evaluate_index(ref self, index: int) -> Value {
        let target = self.evaluate(self.tree.at(index).lhs);
        if (target.is_failure()) {
            return target;
        }
        let subscript = self.evaluate(self.tree.at(index).rhs);
        if (subscript.is_failure()) {
            return subscript;
        }
        if (not subscript.is_number()) {
            return self.fail(ErrorCode.TypeMismatch, "a subscript has to be a number");
        }
        let position = subscript.number.to_int();
        if (target.is_list()) {
            if (position < 0 or position >= target.items.size()) {
                return nil_value();
            }
            return target.items[position];
        }
        if (target.is_text()) {
            if (position < 0 or position >= target.text.size()) {
                return nil_value();
            }
            return text_value(target.text.substr(position, 1));
        }
        return self.fail(ErrorCode.NotIndexable, target.kind_name());
    }

    // Member access is limited to a fixed set of pseudo fields, since the
    // language has no user defined records yet.
    fn evaluate_member(ref self, index: int) -> Value {
        let target = self.evaluate(self.tree.at(index).lhs);
        if (target.is_failure()) {
            return target;
        }
        let field = self.tree.at(index).text;
        if (field == "length" or field == "size") {
            return int_value(target.size());
        }
        if (field == "kind") {
            return text_value(target.kind_name());
        }
        if (field == "first") {
            if (target.is_list() and target.items.size() > 0) {
                return target.items[0];
            }
            return nil_value();
        }
        if (field == "last") {
            if (target.is_list() and target.items.size() > 0) {
                return target.items[target.items.size() - 1];
            }
            return nil_value();
        }
        if (field == "text") {
            return text_value(target.to_text());
        }
        var detail: string = "no member named '";
        detail.append(field);
        detail.append("'");
        return self.fail(ErrorCode.UnknownName, detail);
    }

    fn evaluate_call(ref self, index: int) -> Value {
        let callee = self.tree.at(index).lhs;
        if (self.tree.kind_of(callee) != NodeKind.Variable) {
            return self.fail(ErrorCode.NotCallable, "only named builtins may be called");
        }
        let name = self.tree.at(callee).text;
        var arguments: vector(Value);
        for (child in self.tree.at(index).parts) {
            var argument = child;
            // Unwrap a labelled argument; the label is only documentation.
            if (self.tree.kind_of(argument) == NodeKind.Let) {
                argument = self.tree.at(argument).lhs;
            }
            let value = self.evaluate(argument);
            if (value.is_failure()) {
                return value;
            }
            arguments.push_back(value);
        }
        return self.call_builtin(name, arguments);
    }

    fn call_builtin(ref self, name: string, arguments: vector(Value)) -> Value {
        let wanted = builtin_arity(name);
        if (wanted == -2) {
            var unknown: string = "no builtin named '";
            unknown.append(*name);
            unknown.append("'");
            return self.fail(ErrorCode.NotCallable, unknown);
        }
        if (wanted >= 0 and arguments.size() != wanted) {
            var detail = *name;
            detail.append(" expects ");
            detail.append(to_string(wanted));
            detail.append(" arguments, got ");
            detail.append(to_string(arguments.size()));
            return self.fail(ErrorCode.WrongArity, detail);
        }

        if (*name == "list") {
            var items: vector(Value);
            for (element in *arguments) {
                items.push_back(*element);
            }
            return list_value(move(items));
        }
        if (*name == "print") {
            println(arguments[0].to_text());
            self.output.push_back(arguments[0].to_text());
            return arguments[0];
        }
        if (*name == "type") {
            return text_value(arguments[0].kind_name());
        }
        if (*name == "len") {
            return int_value(arguments[0].size());
        }
        if (*name == "string") {
            return text_value(arguments[0].to_text());
        }
        if (*name == "upper") {
            return text_value(text_upper(arguments[0].to_text()));
        }
        if (*name == "lower") {
            return text_value(text_lower(arguments[0].to_text()));
        }
        if (*name == "trim") {
            return text_value(text_trim(arguments[0].to_text()));
        }
        if (*name == "concat") {
            var joined = arguments[0].to_text();
            joined.append(arguments[1].to_text());
            return text_value(joined);
        }
        if (*name == "at") {
            let container = arguments[0];
            let position = arguments[1].number.to_int();
            if (container.is_list()) {
                if (position < 0 or position >= container.items.size()) {
                    return nil_value();
                }
                return container.items[position];
            }
            if (container.is_text()) {
                if (position < 0 or position >= container.text.size()) {
                    return nil_value();
                }
                return text_value(container.text.substr(position, 1));
            }
            return self.fail(ErrorCode.NotIndexable, container.kind_name());
        }
        if (*name == "slice") {
            let container = arguments[0];
            let begin = arguments[1].number.to_int();
            let count = arguments[2].number.to_int();
            if (container.is_text()) {
                return text_value(container.text.substr(begin, count));
            }
            var items: vector(Value);
            for (i in range(begin, begin + count)) {
                if (i >= 0 and i < container.items.size()) {
                    items.push_back(container.items[i]);
                }
            }
            return list_value(move(items));
        }
        if (*name == "push") {
            var items = arguments[0].items;
            items.push_back(arguments[1]);
            return list_value(move(items));
        }
        if (*name == "sum") {
            var total = decimal_from_int(0);
            for (element in arguments[0].items) {
                if (element.is_number()) {
                    total = decimal_add(total, element.number);
                }
            }
            return number_value(total);
        }

        // The numeric family, all of which want a number argument.
        if (not arguments[0].is_number()) {
            var detail = *name;
            detail.append(" expects a number");
            return self.fail(ErrorCode.TypeMismatch, detail);
        }
        if (*name == "abs") {
            if (arguments[0].number.is_negative()) {
                return number_value(decimal_negate(arguments[0].number));
            }
            return arguments[0];
        }
        if (*name == "floor") {
            return number_value(decimal_floor(arguments[0].number));
        }
        if (*name == "ceil") {
            return number_value(decimal_ceil(arguments[0].number));
        }
        if (*name == "round") {
            return number_value(decimal_round(arguments[0].number));
        }
        if (*name == "sqrt") {
            return number_value(decimal_sqrt(arguments[0].number));
        }
        if (*name == "number") {
            return arguments[0];
        }
        if (*name == "min") {
            if (decimal_compare(arguments[0].number, arguments[1].number) <= 0) {
                return arguments[0];
            }
            return arguments[1];
        }
        if (*name == "max") {
            if (decimal_compare(arguments[0].number, arguments[1].number) >= 0) {
                return arguments[0];
            }
            return arguments[1];
        }
        return self.fail(ErrorCode.NotCallable, *name);
    }

    fn reset(ref self) {
        self.environment = make_environment();
        self.problem = Diagnostic();
        self.output.clear();
        self.steps = 0;
        self.returning = false;
    }

    var tree: Arena;
    var environment: Environment;
    var problem: Diagnostic;
    var output: vector(string);
    var steps: int = 0;
    var step_limit: int = 100000;
    var returning: bool = false;
}

// --- Constant folding ----------------------------------------------------
//
// A bottom up rewrite over the arena. Every fold rewrites a node in place so
// that parents keep pointing at the same index, and the pass reports how
// many nodes it changed so that a caller can iterate to a fixed point.
class Folder {

    fn folded(self) -> int {
        return self.rewrites;
    }

    // A subtree is pure when evaluating it twice has the same effect as
    // evaluating it once and nothing outside it can observe the difference.
    fn pure(self, index: int) -> bool {
        if (not self.tree.valid(index)) {
            return true;
        }
        let kind = self.tree.kind_of(index);
        if (kind == NodeKind.Assign) {
            return false;
        }
        if (kind == NodeKind.Postfix) {
            return false;
        }
        if (kind == NodeKind.Let) {
            return false;
        }
        if (kind == NodeKind.Loop) {
            return false;
        }
        if (kind == NodeKind.Return) {
            return false;
        }
        if (kind == NodeKind.Unary) {
            let op = self.tree.operator_of(index);
            if (op == TokenKind.PlusPlus or op == TokenKind.MinusMinus) {
                return false;
            }
            return self.pure(self.tree.at(index).lhs);
        }
        if (kind == NodeKind.Call) {
            let callee = self.tree.at(index).lhs;
            if (self.tree.kind_of(callee) != NodeKind.Variable) {
                return false;
            }
            if (not builtin_is_pure(self.tree.at(callee).text)) {
                return false;
            }
            for (child in self.tree.at(index).parts) {
                if (not self.pure(child)) {
                    return false;
                }
            }
            return true;
        }
        if (kind == NodeKind.Variable) {
            // A free variable is pure to read but its value is unknown, so
            // the folder treats it as pure and simply cannot fold it.
            return true;
        }
        if (self.tree.at(index).lhs != NO_NODE and not self.pure(self.tree.at(index).lhs)) {
            return false;
        }
        if (self.tree.at(index).rhs != NO_NODE and not self.pure(self.tree.at(index).rhs)) {
            return false;
        }
        if (self.tree.at(index).extra != NO_NODE and not self.pure(self.tree.at(index).extra)) {
            return false;
        }
        return true;
    }

    fn is_constant(self, index: int) -> bool {
        if (not self.tree.valid(index)) {
            return false;
        }
        return self.tree.at(index).is_literal();
    }

    fn constant_value(ref self, index: int) -> Value {
        let kind = self.tree.kind_of(index);
        if (kind == NodeKind.NumberLiteral) {
            return number_value(make_decimal(self.tree.at(index).mantissa, self.tree.at(index).exponent));
        }
        if (kind == NodeKind.TextLiteral) {
            return text_value(self.tree.at(index).text);
        }
        if (kind == NodeKind.BoolLiteral) {
            return bool_value(self.tree.at(index).flag);
        }
        return nil_value();
    }

    fn write_value(ref self, index: int, value: Value) {
        if (value.is_number()) {
            self.tree.replace_with_number(index, value.number.mantissa, value.number.exponent);
            self.rewrites += 1;
            return;
        }
        if (value.is_bool()) {
            self.tree.replace_with_bool(index, value.flag);
            self.rewrites += 1;
            return;
        }
        if (value.is_text()) {
            var literal = Node({.kind = NodeKind.TextLiteral, .text = value.text});
            self.tree.replace(index, move(literal));
            self.rewrites += 1;
            return;
        }
        if (value.is_nil()) {
            self.tree.replace(index, Node({.kind = NodeKind.NullLiteral}));
            self.rewrites += 1;
        }
    }

    // --- The pass --------------------------------------------------------

    fn fold(ref self, index: int) -> int {
        if (not self.tree.valid(index)) {
            return index;
        }
        let kind = self.tree.kind_of(index);

        if (kind == NodeKind.Unary) {
            return self.fold_unary(index);
        }
        if (kind == NodeKind.Binary) {
            return self.fold_binary(index);
        }
        if (kind == NodeKind.Conditional) {
            return self.fold_conditional(index);
        }
        if (kind == NodeKind.Call) {
            return self.fold_call(index);
        }
        if (kind == NodeKind.Block) {
            return self.fold_block(index);
        }
        if (kind == NodeKind.Index) {
            Discard(self.fold(self.tree.at(index).lhs));
            Discard(self.fold(self.tree.at(index).rhs));
            return index;
        }
        if (kind == NodeKind.Member) {
            Discard(self.fold(self.tree.at(index).lhs));
            return index;
        }
        if (kind == NodeKind.Assign) {
            Discard(self.fold(self.tree.at(index).rhs));
            return index;
        }
        if (kind == NodeKind.Let) {
            Discard(self.fold(self.tree.at(index).lhs));
            return index;
        }
        if (kind == NodeKind.Loop) {
            return self.fold_loop(index);
        }
        if (kind == NodeKind.Return) {
            Discard(self.fold(self.tree.at(index).lhs));
            return index;
        }
        return index;
    }

    fn fold_unary(ref self, index: int) -> int {
        let operand = self.fold(self.tree.at(index).lhs);
        let op = self.tree.operator_of(index);

        // Double negation cancels, which shows up constantly in generated
        // input like `-(-x)`.
        if (op == TokenKind.Minus and self.tree.kind_of(operand) == NodeKind.Unary) {
            if (self.tree.operator_of(operand) == TokenKind.Minus) {
                let inner = self.tree.at(operand).lhs;
                let copy = self.tree.adopt(inner);
                var replacement = Node({.kind = self.tree.kind_of(copy)});
                replacement.op = self.tree.operator_of(copy);
                replacement.lhs = self.tree.at(copy).lhs;
                replacement.rhs = self.tree.at(copy).rhs;
                replacement.extra = self.tree.at(copy).extra;
                replacement.mantissa = self.tree.at(copy).mantissa;
                replacement.exponent = self.tree.at(copy).exponent;
                replacement.text = self.tree.at(copy).text;
                replacement.flag = self.tree.at(copy).flag;
                self.tree.replace(index, move(replacement));
                self.rewrites += 1;
                return index;
            }
        }

        if (not self.is_constant(operand)) {
            return index;
        }
        var machine = Interpreter();
        machine.tree = self.tree;
        let value = machine.evaluate(index);
        if (value.is_failure()) {
            return index;
        }
        self.write_value(index, value);
        return index;
    }

    fn fold_binary(ref self, index: int) -> int {
        let left = self.fold(self.tree.at(index).lhs);
        let right = self.fold(self.tree.at(index).rhs);
        let op = self.tree.operator_of(index);

        if (self.is_constant(left) and self.is_constant(right)) {
            let a = self.constant_value(left);
            let b = self.constant_value(right);
            if (op == TokenKind.Slash and b.is_number() and b.number.is_zero()) {
                return index;
            }
            if (op == TokenKind.Percent and b.is_number() and b.number.is_zero()) {
                return index;
            }
            var machine = Interpreter();
            machine.tree = self.tree;
            let value = machine.evaluate(index);
            if (not value.is_failure()) {
                self.write_value(index, value);
            }
            return index;
        }

        // Algebraic identities, which fire far more often than full folds.
        if (self.is_zero_literal(right) and (op == TokenKind.Plus or op == TokenKind.Minus)) {
            self.splice(index, left);
            return index;
        }
        if (self.is_zero_literal(left) and op == TokenKind.Plus) {
            self.splice(index, right);
            return index;
        }
        if (self.is_one_literal(right) and (op == TokenKind.Star or op == TokenKind.Slash)) {
            self.splice(index, left);
            return index;
        }
        if (self.is_one_literal(left) and op == TokenKind.Star) {
            self.splice(index, right);
            return index;
        }
        if (self.is_zero_literal(right) and op == TokenKind.Star and self.pure(left)) {
            self.tree.replace_with_number(index, 0, 0);
            self.rewrites += 1;
            return index;
        }
        if (op == TokenKind.AmpAmp and self.is_false_literal(left)) {
            self.tree.replace_with_bool(index, false);
            self.rewrites += 1;
            return index;
        }
        if (op == TokenKind.PipePipe and self.is_true_literal(left)) {
            self.tree.replace_with_bool(index, true);
            self.rewrites += 1;
            return index;
        }
        return index;
    }

    fn fold_conditional(ref self, index: int) -> int {
        let condition = self.fold(self.tree.at(index).lhs);
        let whenever = self.fold(self.tree.at(index).rhs);
        let otherwise = self.fold(self.tree.at(index).extra);
        if (not self.is_constant(condition)) {
            return index;
        }
        // Dead branch elimination: a constant condition means one of the two
        // arms can never run, so the surviving arm replaces the whole node.
        let decided = self.constant_value(condition);
        if (decided.truthy()) {
            self.splice(index, whenever);
        } else {
            self.splice(index, otherwise);
        }
        return index;
    }

    fn fold_call(ref self, index: int) -> int {
        var all_constant = true;
        for (child in self.tree.at(index).parts) {
            let folded = self.fold(child);
            if (not self.is_constant(folded)) {
                all_constant = false;
            }
        }
        let callee = self.tree.at(index).lhs;
        if (self.tree.kind_of(callee) != NodeKind.Variable) {
            return index;
        }
        if (not builtin_is_pure(self.tree.at(callee).text)) {
            return index;
        }
        if (not all_constant) {
            return index;
        }
        var machine = Interpreter();
        machine.tree = self.tree;
        let value = machine.evaluate(index);
        if (value.is_failure()) {
            return index;
        }
        if (value.is_list()) {
            return index;
        }
        self.write_value(index, value);
        return index;
    }

    // `while false { ... }` never runs at all.
    fn fold_loop(ref self, index: int) -> int {
        let condition = self.fold(self.tree.at(index).lhs);
        Discard(self.fold(self.tree.at(index).rhs));
        if (self.is_false_literal(condition)) {
            self.tree.replace(index, Node({.kind = NodeKind.Empty}));
            self.rewrites += 1;
        }
        return index;
    }

    // Dead code within a block: statements after a `return` can never run,
    // and pure statements whose value is discarded can be dropped outright.
    fn fold_block(ref self, index: int) -> int {
        var kept: vector(int);
        var terminated = false;
        let total = self.tree.at(index).parts.size();
        var position: int = 0;
        for (child in self.tree.at(index).parts) {
            position += 1;
            if (terminated) {
                self.removed += 1;
                continue;
            }
            let folded = self.fold(child);
            let kind = self.tree.kind_of(folded);
            if (kind == NodeKind.Empty) {
                self.removed += 1;
                continue;
            }
            // The final statement is the value of the block, so it stays
            // even when it is pure.
            if (position < total and self.pure(folded) and self.droppable(folded)) {
                self.removed += 1;
                continue;
            }
            kept.push_back(folded);
            if (kind == NodeKind.Return) {
                terminated = true;
            }
        }
        let ref slot: auto = self.tree.at(index);
        slot.parts.clear();
        for (survivor in kept) {
            slot.parts.push_back(survivor);
        }
        return index;
    }

    // Statements worth removing when their value is unused. A bare variable
    // or literal qualifies; a call never does, even a pure one, because the
    // author probably meant it.
    fn droppable(self, index: int) -> bool {
        let kind = self.tree.kind_of(index);
        if (kind == NodeKind.Variable) { return true; }
        if (kind == NodeKind.NumberLiteral) { return true; }
        if (kind == NodeKind.TextLiteral) { return true; }
        if (kind == NodeKind.BoolLiteral) { return true; }
        if (kind == NodeKind.NullLiteral) { return true; }
        if (kind == NodeKind.Empty) { return true; }
        return false;
    }

    // Copy `source` over `target` so that the parent keeps its index.
    fn splice(ref self, target: int, source: int) {
        if (not self.tree.valid(source)) {
            return;
        }
        var replacement = Node({.kind = self.tree.kind_of(source)});
        replacement.op = self.tree.operator_of(source);
        replacement.lhs = self.tree.at(source).lhs;
        replacement.rhs = self.tree.at(source).rhs;
        replacement.extra = self.tree.at(source).extra;
        replacement.mantissa = self.tree.at(source).mantissa;
        replacement.exponent = self.tree.at(source).exponent;
        replacement.text = self.tree.at(source).text;
        replacement.flag = self.tree.at(source).flag;
        for (child in self.tree.at(source).parts) {
            replacement.parts.push_back(child);
        }
        self.tree.replace(target, move(replacement));
        self.rewrites += 1;
    }

    fn is_zero_literal(self, index: int) -> bool {
        if (self.tree.kind_of(index) != NodeKind.NumberLiteral) {
            return false;
        }
        return self.tree.at(index).mantissa == 0;
    }

    fn is_one_literal(self, index: int) -> bool {
        if (self.tree.kind_of(index) != NodeKind.NumberLiteral) {
            return false;
        }
        var probe = make_decimal(self.tree.at(index).mantissa, self.tree.at(index).exponent);
        return decimal_equal(probe, decimal_from_int(1));
    }

    fn is_true_literal(self, index: int) -> bool {
        if (self.tree.kind_of(index) != NodeKind.BoolLiteral) {
            return false;
        }
        return self.tree.at(index).flag;
    }

    fn is_false_literal(self, index: int) -> bool {
        if (self.tree.kind_of(index) != NodeKind.BoolLiteral) {
            return false;
        }
        return not self.tree.at(index).flag;
    }

    var tree: Arena;
    var rewrites: int = 0;
    var removed: int = 0;
}

// Run the folder until it stops making progress, or until the round limit is
// hit. Two rounds are usually enough; the third only confirms the fixed
// point.
let MAX_FOLD_ROUNDS: int = 4;

fn fold_to_fixed_point(ref tree: Arena, root: int) -> int {
    var total: int = 0;
    var round: int = 0;
    while (round < MAX_FOLD_ROUNDS) {
        var pass = Folder();
        pass.tree = *tree;
        Discard(pass.fold(root));
        *tree = move(pass.tree);
        total += pass.rewrites;
        if (pass.rewrites == 0) {
            break;
        }
        round += 1;
    }
    return total;
}

// --- The printer ---------------------------------------------------------
//
// Turns a tree back into source text. The output is not the original text -
// spacing is normalised and redundant parentheses are dropped - but it is
// guaranteed to reparse into the same tree, which is what the round trip
// test in this file checks.
//
// Parentheses are reinserted from the same precedence table the parser used:
// a child is wrapped when its own binding power is lower than the level it
// appears in, or when it sits on the wrong side of a left associative
// operator.
class Printer {

    fn print(ref self, index: int) -> string {
        return self.print_at(index, PRECEDENCE_NONE);
    }

    fn print_at(ref self, index: int, context_power: int) -> string {
        if (not self.tree.valid(index)) {
            return TEXT_EMPTY;
        }
        let kind = self.tree.kind_of(index);

        if (kind == NodeKind.NumberLiteral) {
            return self.print_number(index);
        }
        if (kind == NodeKind.TextLiteral) {
            return quote_text(self.tree.at(index).text);
        }
        if (kind == NodeKind.BoolLiteral) {
            if (self.tree.at(index).flag) {
                return WORD_TRUE;
            }
            return WORD_FALSE;
        }
        if (kind == NodeKind.NullLiteral) {
            return WORD_NULL;
        }
        if (kind == NodeKind.Empty) {
            return TEXT_SEMICOLON;
        }
        if (kind == NodeKind.Variable) {
            return self.tree.at(index).text;
        }
        if (kind == NodeKind.Unary) {
            return self.print_unary(index, context_power);
        }
        if (kind == NodeKind.Postfix) {
            return self.print_postfix(index);
        }
        if (kind == NodeKind.Binary) {
            return self.print_binary(index, context_power);
        }
        if (kind == NodeKind.Assign) {
            return self.print_assign(index, context_power);
        }
        if (kind == NodeKind.Conditional) {
            return self.print_conditional(index, context_power);
        }
        if (kind == NodeKind.Call) {
            return self.print_call(index);
        }
        if (kind == NodeKind.Index) {
            return self.print_index(index);
        }
        if (kind == NodeKind.Member) {
            return self.print_member(index);
        }
        if (kind == NodeKind.Let) {
            return self.print_let(index);
        }
        if (kind == NodeKind.Block) {
            return self.print_block(index);
        }
        if (kind == NodeKind.Loop) {
            return self.print_loop(index);
        }
        if (kind == NodeKind.Return) {
            var out: string = "return ";
            out.append(self.print_at(self.tree.at(index).lhs, PRECEDENCE_NONE));
            return out;
        }
        return "<invalid>";
    }

    fn print_number(ref self, index: int) -> string {
        var value = make_decimal(self.tree.at(index).mantissa, self.tree.at(index).exponent);
        return value.to_text();
    }

    fn print_unary(ref self, index: int, context_power: int) -> string {
        var out = token_spelling(self.tree.operator_of(index));
        // `not x` needs the separating space that `!x` does not.
        if (self.tree.operator_of(index) == TokenKind.KeywordNot) {
            out.append(TEXT_SPACE);
        }
        let operand = self.tree.at(index).lhs;
        // `- -x` must not print as `--x`, which would relex as a decrement.
        if (self.tree.kind_of(operand) == NodeKind.Unary) {
            if (self.tree.operator_of(operand) == self.tree.operator_of(index)) {
                out.append(TEXT_SPACE);
            }
        }
        out.append(self.print_at(operand, PRECEDENCE_PREFIX));
        return self.wrap_if(out, PRECEDENCE_PREFIX, context_power);
    }

    fn print_postfix(ref self, index: int) -> string {
        var out = self.print_at(self.tree.at(index).lhs, PRECEDENCE_POSTFIX);
        out.append(token_spelling(self.tree.operator_of(index)));
        return out;
    }

    fn print_binary(ref self, index: int, context_power: int) -> string {
        let op = self.tree.operator_of(index);
        let power = infix_precedence(op);
        let right_associative = infix_associativity(op) == Associativity.Right;

        var left_power = power;
        var right_power = power + 1;
        if (right_associative) {
            left_power = power + 1;
            right_power = power;
        }

        var out = self.print_at(self.tree.at(index).lhs, left_power);
        if (self.spaced_operator(op)) {
            out.append(TEXT_SPACE);
            out.append(token_spelling(op));
            out.append(TEXT_SPACE);
        } else {
            out.append(token_spelling(op));
        }
        out.append(self.print_at(self.tree.at(index).rhs, right_power));
        return self.wrap_if(out, power, context_power);
    }

    fn print_assign(ref self, index: int, context_power: int) -> string {
        var out = self.print_at(self.tree.at(index).lhs, PRECEDENCE_ASSIGN + 1);
        out.append(TEXT_SPACE);
        out.append(token_spelling(self.tree.operator_of(index)));
        out.append(TEXT_SPACE);
        out.append(self.print_at(self.tree.at(index).rhs, PRECEDENCE_ASSIGN));
        return self.wrap_if(out, PRECEDENCE_ASSIGN, context_power);
    }

    // The `flag` field records which of the two surface forms the input
    // used, so the round trip preserves the author's choice.
    fn print_conditional(ref self, index: int, context_power: int) -> string {
        var out: string = "";
        if (self.tree.at(index).flag) {
            out.append(self.print_at(self.tree.at(index).lhs, PRECEDENCE_CONDITIONAL + 1));
            out.append(TEXT_QUESTION_SPACE);
            out.append(self.print_at(self.tree.at(index).rhs, PRECEDENCE_NONE));
            out.append(TEXT_SPACE_COLON_SPACE);
            out.append(self.print_at(self.tree.at(index).extra, PRECEDENCE_CONDITIONAL));
            return self.wrap_if(out, PRECEDENCE_CONDITIONAL, context_power);
        }
        out.append(WORD_IF);
        out.append(TEXT_SPACE);
        out.append(self.print_at(self.tree.at(index).lhs, PRECEDENCE_CONDITIONAL));
        out.append(TEXT_SPACE);
        out.append(WORD_THEN);
        out.append(TEXT_SPACE);
        out.append(self.print_at(self.tree.at(index).rhs, PRECEDENCE_CONDITIONAL));
        if (self.tree.kind_of(self.tree.at(index).extra) != NodeKind.NullLiteral) {
            out.append(TEXT_SPACE);
            out.append(WORD_ELSE);
            out.append(TEXT_SPACE);
            out.append(self.print_at(self.tree.at(index).extra, PRECEDENCE_CONDITIONAL));
        }
        return self.wrap_if(out, PRECEDENCE_CONDITIONAL, context_power);
    }

    fn print_call(ref self, index: int) -> string {
        var out = self.print_at(self.tree.at(index).lhs, PRECEDENCE_POSTFIX);
        out.append(TEXT_LPAREN);
        var first = true;
        for (child in self.tree.at(index).parts) {
            if (not first) {
                out.append(TEXT_COMMA_SPACE);
            }
            out.append(self.print_argument(child));
            first = false;
        }
        out.append(TEXT_RPAREN);
        return out;
    }

    fn print_argument(ref self, index: int) -> string {
        if (self.tree.kind_of(index) == NodeKind.Let and self.tree.at(index).flag) {
            var out = self.tree.at(index).text;
            out.append(TEXT_COLON_SPACE);
            out.append(self.print_at(self.tree.at(index).lhs, PRECEDENCE_ASSIGN));
            return out;
        }
        return self.print_at(index, PRECEDENCE_ASSIGN);
    }

    fn print_index(ref self, index: int) -> string {
        var out = self.print_at(self.tree.at(index).lhs, PRECEDENCE_POSTFIX);
        out.append(TEXT_LBRACKET);
        out.append(self.print_at(self.tree.at(index).rhs, PRECEDENCE_NONE));
        out.append(TEXT_RBRACKET);
        return out;
    }

    fn print_member(ref self, index: int) -> string {
        var out = self.print_at(self.tree.at(index).lhs, PRECEDENCE_POSTFIX);
        out.append(TEXT_DOT);
        out.append(self.tree.at(index).text);
        return out;
    }

    fn print_let(ref self, index: int) -> string {
        var out = WORD_LET;
        out.append(TEXT_SPACE);
        out.append(self.tree.at(index).text);
        out.append(" = ");
        out.append(self.print_at(self.tree.at(index).lhs, PRECEDENCE_NONE));
        return out;
    }

    fn print_loop(ref self, index: int) -> string {
        var out = WORD_WHILE;
        out.append(TEXT_SPACE);
        out.append(self.print_at(self.tree.at(index).lhs, PRECEDENCE_NONE));
        out.append(TEXT_SPACE);
        out.append(self.print_at(self.tree.at(index).rhs, PRECEDENCE_NONE));
        return out;
    }

    fn print_block(ref self, index: int) -> string {
        if (self.tree.at(index).parts.empty()) {
            return "{}";
        }
        var out = TEXT_LBRACE;
        out.append(TEXT_NEWLINE);
        self.indent += 1;
        for (statement in self.tree.at(index).parts) {
            out.append(self.padding());
            out.append(self.print_at(statement, PRECEDENCE_NONE));
            out.append(TEXT_SEMICOLON);
            out.append(TEXT_NEWLINE);
        }
        self.indent -= 1;
        out.append(self.padding());
        out.append(TEXT_RBRACE);
        return out;
    }

    fn padding(self) -> string {
        var out: string = "";
        for (i in range(0, self.indent)) {
            out.append(TEXT_INDENT);
            Discard(i);
        }
        return out;
    }

    // Operators that read better without surrounding spaces at their
    // precedence level. Multiplication is tight, addition is not.
    fn spaced_operator(self, op: TokenKind) -> bool {
        if (not self.compact) {
            return true;
        }
        if (infix_precedence(op) >= PRECEDENCE_MULTIPLICATIVE) {
            return false;
        }
        return true;
    }

    fn wrap_if(self, body: string, power: int, context_power: int) -> string {
        if (power >= context_power) {
            return *body;
        }
        var out = TEXT_LPAREN;
        out.append(*body);
        out.append(TEXT_RPAREN);
        return out;
    }

    var tree: Arena;
    var indent: int = 0;
    var compact: bool = false;
}

// Re-escape a string literal so that it lexes back to the same contents.
fn quote_text(text: string) -> string {
    var out: string = "";
    out.push_back(CHAR_QUOTE);
    for (i in range(0, text.size())) {
        let c = text.at(i);
        let escaped = encode_escape(c);
        if (escaped.size() > 0) {
            out.append(escaped);
        } else {
            out.push_back(c);
        }
    }
    out.push_back(CHAR_QUOTE);
    return out;
}

fn print_tree(ref tree: Arena, root: int) -> string {
    var writer = Printer();
    writer.tree = *tree;
    return writer.print(root);
}

// --- Structural dumping --------------------------------------------------
//
// The other direction: an indented tree dump, used when a test fails and the
// flat source form is not enough to see what went wrong.
fn dump_node(ref tree: Arena, index: int, depth: int) -> string {
    var out: string = "";
    for (i in range(0, depth)) {
        out.append(TEXT_INDENT);
        Discard(i);
    }
    if (not tree.valid(index)) {
        out.append("<none>\n");
        return out;
    }
    out.append(node_kind_name(tree.kind_of(index)));
    let op = tree.operator_of(index);
    if (op != TokenKind.Invalid) {
        out.append(" '");
        out.append(token_spelling(op));
        out.append("'");
    }
    if (tree.at(index).text.size() > 0) {
        out.append(" ");
        out.append(tree.at(index).text);
    }
    if (tree.kind_of(index) == NodeKind.NumberLiteral) {
        out.append(" ");
        out.append(to_string(tree.at(index).mantissa));
        out.append("e");
        out.append(to_string(tree.at(index).exponent));
    }
    out.append(TEXT_NEWLINE);
    if (tree.at(index).lhs != NO_NODE) {
        out.append(dump_node(tree, tree.at(index).lhs, depth + 1));
    }
    if (tree.at(index).rhs != NO_NODE) {
        out.append(dump_node(tree, tree.at(index).rhs, depth + 1));
    }
    if (tree.at(index).extra != NO_NODE) {
        out.append(dump_node(tree, tree.at(index).extra, depth + 1));
    }
    for (child in tree.at(index).parts) {
        out.append(dump_node(tree, child, depth + 1));
    }
    return out;
}

// Structural comparison, which is what "round trips" actually means: the
// text may differ, the tree may not.
fn trees_equal(ref left: Arena, a: int, ref right: Arena, b: int) -> bool {
    if (not left.valid(a) or not right.valid(b)) {
        return left.valid(a) == right.valid(b);
    }
    if (left.kind_of(a) != right.kind_of(b)) {
        return false;
    }
    if (left.operator_of(a) != right.operator_of(b)) {
        return false;
    }
    if (left.at(a).text != right.at(b).text) {
        return false;
    }
    if (left.kind_of(a) == NodeKind.NumberLiteral) {
        let x = make_decimal(left.at(a).mantissa, left.at(a).exponent);
        let y = make_decimal(right.at(b).mantissa, right.at(b).exponent);
        if (not decimal_equal(x, y)) {
            return false;
        }
    }
    if (left.at(a).flag != right.at(b).flag) {
        return false;
    }
    if (left.at(a).parts.size() != right.at(b).parts.size()) {
        return false;
    }
    if (not trees_equal(left, left.at(a).lhs, right, right.at(b).lhs)) {
        return false;
    }
    if (not trees_equal(left, left.at(a).rhs, right, right.at(b).rhs)) {
        return false;
    }
    if (not trees_equal(left, left.at(a).extra, right, right.at(b).extra)) {
        return false;
    }
    for (i in range(0, left.at(a).parts.size())) {
        if (not trees_equal(left, left.at(a).parts[i], right, right.at(b).parts[i])) {
            return false;
        }
    }
    return true;
}

// --- Driving the whole pipeline ------------------------------------------
//
// One object that owns a parser, a folder and an interpreter, so that a
// caller can feed it one line at a time and keep the bindings between calls.
class Session {

    fn ready(self) -> bool {
        return self.problem.ok();
    }

    fn last_error(self) -> string {
        return self.problem.describe();
    }

    // Parse, optionally fold, then evaluate. Returns the value of the last
    // statement, or a failure value describing the first thing that went
    // wrong.
    fn run(ref self, source: string) -> Value {
        var parser: Parser;
        let root = parser.parse_program(source);
        if (not parser.problem.ok()) {
            self.problem = parser.problem;
            return failure_value(self.problem.describe());
        }
        if (self.optimize) {
            self.rewrites += fold_to_fixed_point(parser.tree, root);
        }
        self.machine.tree = move(parser.tree);
        self.machine.returning = false;
        let value = self.machine.evaluate(root);
        self.steps += self.machine.steps;
        if (value.is_failure()) {
            self.problem = self.machine.problem;
        }
        self.last = value;
        return value;
    }

    fn define(ref self, name: string, value: Value) {
        self.machine.environment.define_global(name, move(value));
    }

    fn bound(self, name: string) -> bool {
        return self.machine.environment.bound(name);
    }

    fn value_of(ref self, name: string) -> ref Value {
        return self.machine.environment.lookup(name);
    }

    fn reset(ref self) {
        self.machine.reset();
        self.problem = Diagnostic();
        self.steps = 0;
        self.rewrites = 0;
        self.last = nil_value();
    }

    var machine: Interpreter;
    var problem: Diagnostic;
    var last: Value;
    var steps: int = 0;
    var rewrites: int = 0;
    var optimize: bool = true;
}

fn make_session() -> Session {
    var session: Session;
    session.machine.environment = make_environment();
    return session;
}

// Convenience: evaluate one expression in a throwaway session.
fn evaluate_text(source: string) -> Value {
    var session = make_session();
    return session.run(source);
}

// --- The round trip ------------------------------------------------------
//
// Parse, print, reparse and compare. This is the property the printer has to
// satisfy: printing may change the text but it may never change the tree.
fn round_trips(source: string) -> bool {
    var first: Parser;
    let a = first.parse_source(source);
    if (not first.problem.ok()) {
        return false;
    }
    let printed = print_tree(first.tree, a);

    var second: Parser;
    let b = second.parse_source(printed);
    if (not second.problem.ok()) {
        return false;
    }
    return trees_equal(first.tree, a, second.tree, b);
}

// The stronger version: printing the reprinted text has to be byte identical
// to the first printing, so the printer is idempotent as well as faithful.
fn printing_is_stable(source: string) -> bool {
    var first: Parser;
    let a = first.parse_source(source);
    if (not first.problem.ok()) {
        return false;
    }
    let once = print_tree(first.tree, a);

    var second: Parser;
    let b = second.parse_source(once);
    if (not second.problem.ok()) {
        return false;
    }
    let twice = print_tree(second.tree, b);
    return once == twice;
}

// --- Sample programs -----------------------------------------------------
//
// Deliberately dense inputs that exercise the whole precedence table at
// once. They are held as strings, so the operators inside them are data for
// the tokenizer rather than charge syntax.
fn sample_expressions() -> vector(string) {
    var samples: vector(string);
    samples.push_back("1 + 2 * 3 - 4 / 2");
    samples.push_back("2 ** 3 ** 2");
    samples.push_back("-x * -y + ~mask");
    samples.push_back("a < b && b < c || not done");
    samples.push_back("flags & 12 | 3 ^ 5");
    samples.push_back("value << 3 >> 1");
    samples.push_back("total += count * 2");
    samples.push_back("if ready then 1 else 0");
    samples.push_back("ready ? first : second");
    samples.push_back("points[0].x + points[1].y");
    samples.push_back("min(a, b) + max(c, d)");
    samples.push_back("offset(dx: 3, dy: 4)");
    samples.push_back("\"prefix \" + name + \" suffix\"");
    samples.push_back("counter++ + ++counter");
    samples.push_back("5! + 3!");
    samples.push_back("12.5 * 4 - 0.5");
    samples.push_back("list(1, 2, 3).length");
    samples.push_back("[1, 2, 3][1]");
    samples.push_back("(a + b) * (c - d)");
    samples.push_back("a = b = c = 0");
    samples.push_back("sum([10, 20, 30]) / 3");
    samples.push_back("upper(trim(\"  hello  \"))");
    return samples;
}

fn sample_programs() -> vector(string) {
    var programs: vector(string);
    programs.push_back("let x = 3; let y = 4; sqrt(x ** 2 + y ** 2)");
    programs.push_back("let total = 0; let i = 0; while i < 10 { total += i; i++; } total");
    programs.push_back("let name = \"world\"; concat(\"hello \", name)");
    programs.push_back("let items = [1, 2, 3, 4]; sum(items) / len(items)");
    programs.push_back("let flag = true; if flag then \"yes\" else \"no\"");
    programs.push_back("let scale = 1000; let ratio = 3141 / scale; round(ratio)");
    // A block whose tail is dead: the folder should drop everything after
    // the return.
    programs.push_back("{ return 1; let unused = 2; unused + 3; }");
    return programs;
}

// --- Self checks ---------------------------------------------------------
//
// These read like unit tests but they are ordinary functions; the benchmark
// harness calls them and the assertions document the intended behaviour.

fn check_precedence_table() {
    Assert(infix_precedence(TokenKind.Star) > infix_precedence(TokenKind.Plus));
    Assert(infix_precedence(TokenKind.Plus) > infix_precedence(TokenKind.LessLess));
    Assert(infix_precedence(TokenKind.LessLess) > infix_precedence(TokenKind.Less));
    Assert(infix_precedence(TokenKind.Less) > infix_precedence(TokenKind.EqualEqual));
    Assert(infix_precedence(TokenKind.EqualEqual) > infix_precedence(TokenKind.Amp));
    Assert(infix_precedence(TokenKind.Amp) > infix_precedence(TokenKind.Caret));
    Assert(infix_precedence(TokenKind.Caret) > infix_precedence(TokenKind.Pipe));
    Assert(infix_precedence(TokenKind.Pipe) > infix_precedence(TokenKind.AmpAmp));
    Assert(infix_precedence(TokenKind.AmpAmp) > infix_precedence(TokenKind.PipePipe));
    Assert(infix_precedence(TokenKind.PipePipe) > infix_precedence(TokenKind.Question));
    Assert(infix_precedence(TokenKind.Question) > infix_precedence(TokenKind.Equal));
    Assert(infix_precedence(TokenKind.StarStar) > infix_precedence(TokenKind.Star));
    Assert(prefix_precedence(TokenKind.Bang) > infix_precedence(TokenKind.StarStar));
    Assert(postfix_precedence(TokenKind.LParen) > prefix_precedence(TokenKind.Bang));
    Prove(infix_precedence(TokenKind.Identifier) == PRECEDENCE_NONE);
    Prove(infix_associativity(TokenKind.StarStar) == Associativity.Right);
    Prove(infix_associativity(TokenKind.Minus) == Associativity.Left);
}

fn check_table_is_consistent() {
    let rows = operator_table();
    for (row in rows) {
        Assert(row.precedence == infix_precedence(row.kind));
        Assert(row.associativity == infix_associativity(row.kind));
        Assert(row.spelling == token_spelling(row.kind));
        if (row.mutating) {
            Assert(is_assignment_operator(row.kind));
        }
    }
}

fn check_tokenizer() {
    let tokens = tokenize("a + 12.5 * \"text\" // trailing");
    Assert(tokens.size() > 0);
    Assert(tokens[0].kind == TokenKind.Identifier);
    Assert(tokens[1].kind == TokenKind.Plus);
    Assert(tokens[2].kind == TokenKind.Number);
    Assert(tokens[2].mantissa == 125);
    Assert(tokens[2].exponent == -1);
    Assert(tokens[3].kind == TokenKind.Star);
    Assert(tokens[4].kind == TokenKind.Text);
    Assert(tokens[tokens.size() - 1].kind == TokenKind.End);
}

fn check_maximal_munch() {
    let tokens = tokenize("a <<= b >>= c ++ d -- e ** f");
    Assert(tokens[1].kind == TokenKind.LessLessEqual);
    Assert(tokens[3].kind == TokenKind.GreaterGreaterEqual);
    Assert(tokens[5].kind == TokenKind.PlusPlus);
    Assert(tokens[7].kind == TokenKind.MinusMinus);
    Assert(tokens[9].kind == TokenKind.StarStar);
}

fn check_decimals() {
    let a = make_decimal(125, -1);
    let b = make_decimal(5, -1);
    Assert(decimal_add(a, b).to_text() == "13");
    Assert(decimal_subtract(a, b).to_text() == "12");
    Assert(decimal_multiply(a, decimal_from_int(2)).to_text() == "25");
    Assert(decimal_compare(a, b) > 0);
    Assert(decimal_equal(make_decimal(150, -2), make_decimal(15, -1)));
    Prove(decimal_from_int(0).is_zero());
}

fn check_parses() {
    for (source in sample_expressions()) {
        var parser: Parser;
        let root = parser.parse_source(source);
        Assert(root != NO_NODE);
        Assert(parser.problem.ok());
    }
}

fn check_round_trip() {
    for (source in sample_expressions()) {
        Assert(round_trips(source));
        Assert(printing_is_stable(source));
    }
}

fn check_associativity_shape() {
    // `2 ** 3 ** 2` groups to the right, so the left child is a literal and
    // the right child is another power node.
    var parser: Parser;
    let root = parser.parse_source("2 ** 3 ** 2");
    Assert(parser.tree.kind_of(root) == NodeKind.Binary);
    Assert(parser.tree.operator_of(root) == TokenKind.StarStar);
    let right = parser.tree.at(root).rhs;
    Assert(parser.tree.kind_of(right) == NodeKind.Binary);
    Assert(parser.tree.operator_of(right) == TokenKind.StarStar);

    // Subtraction groups to the left, so the shape is mirrored.
    var other: Parser;
    let flat = other.parse_source("1 - 2 - 3");
    Assert(other.tree.kind_of(other.tree.at(flat).lhs) == NodeKind.Binary);
    Assert(other.tree.kind_of(other.tree.at(flat).rhs) == NodeKind.NumberLiteral);
}

fn check_folding() {
    var parser: Parser;
    let root = parser.parse_source("1 + 2 * 3");
    let changes = fold_to_fixed_point(parser.tree, root);
    Assert(changes > 0);
    Assert(parser.tree.kind_of(root) == NodeKind.NumberLiteral);
    Assert(parser.tree.at(root).mantissa == 7);
}

fn check_identity_folding() {
    var parser: Parser;
    let root = parser.parse_source("x * 1 + 0");
    Discard(fold_to_fixed_point(parser.tree, root));
    Assert(parser.tree.kind_of(root) == NodeKind.Variable);
    Assert(parser.tree.at(root).text == "x");
}

fn check_dead_branch() {
    var parser: Parser;
    let root = parser.parse_source("if true then 1 else 2");
    Discard(fold_to_fixed_point(parser.tree, root));
    Assert(parser.tree.kind_of(root) == NodeKind.NumberLiteral);
    Assert(parser.tree.at(root).mantissa == 1);
}

fn check_evaluation() {
    Assert(evaluate_text("1 + 2 * 3").to_text() == "7");
    Assert(evaluate_text("2 ** 10").to_text() == "1024");
    Assert(evaluate_text("10 / 4").to_text() == "2.5");
    Assert(evaluate_text("7 % 3").to_text() == "1");
    Assert(evaluate_text("\"a\" + \"b\"").to_text() == "ab");
    Assert(evaluate_text("len([1, 2, 3])").to_text() == "3");
    Assert(evaluate_text("true && false").to_text() == WORD_FALSE);
    Assert(evaluate_text("1 < 2 && 2 < 3").truthy());
}

fn check_scoping() {
    var session = make_session();
    Discard(session.run("let outer = 1;"));
    Assert(session.bound("outer"));
    Discard(session.run("{ let inner = 2; inner + outer; }"));
    // The inner binding died with its block; the outer one survived.
    Assert(session.bound("outer"));
    Assert(not session.bound("inner"));
}

fn check_errors() {
    var parser: Parser;
    let root = parser.parse_source("1 +");
    Assert(root == NO_NODE or not parser.problem.ok());
    Assert(parser.problem.code == ErrorCode.ExpectedExpression);

    var second: Parser;
    Discard(second.parse_source("(1 + 2"));
    Assert(second.problem.code == ErrorCode.UnbalancedParen);

    var third: Parser;
    Discard(third.parse_source("1 = 2"));
    Assert(third.problem.code == ErrorCode.NotAssignable);
}

fn check_programs() {
    for (source in sample_programs()) {
        var session = make_session();
        let value = session.run(source);
        Assert(not value.is_failure());
    }
}

// The entry point the benchmark harness calls. Everything above is reachable
// from here.
fn run_all_checks() {
    check_precedence_table();
    check_table_is_consistent();
    check_tokenizer();
    check_maximal_munch();
    check_decimals();
    check_parses();
    check_round_trip();
    check_associativity_shape();
    check_folding();
    check_identity_folding();
    check_dead_branch();
    check_evaluation();
    check_scoping();
    check_errors();
    check_programs();
    println("expression language: all checks passed");
}

// A tiny read eval print driver, kept at the bottom so the file ends on the
// thing a reader is most likely to look for.
fn repl(lines: vector(string)) -> int {
    var session = make_session();
    var failures: int = 0;
    for (line in *lines) {
        let trimmed = text_trim(line);
        if (trimmed.empty()) {
            continue;
        }
        let value = session.run(trimmed);
        if (value.is_failure()) {
            failures += 1;
            println(session.last_error());
        } else {
            println(value.to_text());
        }
    }
    return failures;
}


