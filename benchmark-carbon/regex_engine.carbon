// A complete regular expression engine: pattern lexer, recursive descent
// pattern parser, Thompson NFA construction, NFA simulation over a state set,
// a backtracking matcher with capture groups, a compiled instruction program
// with a small optimiser, and the user facing match/search/replace/split API.
//
// The engine works on `string` values and addresses them by integer offsets.
// Every structure that would normally hold pointers instead holds indices into
// a flat arena, so patterns are trivially copyable and can be serialised.
//
// Supported syntax:
//
//     literal        a  b  \+  \{
//     any            .
//     class          [abc]  [^abc]  [a-z0-9_]  [[:alpha:]]
//     shorthand      \d \D \w \W \s \S \h \u{0B}
//     group          (...)  (?:...)  (?<name>...)
//     alternation    a|b|c
//     repetition     a*  a+  a?  a{2}  a{2,}  a{2,5}  and lazy forms a*?
//     anchors        ^  $  \u{08}  \B  \A  \z
//     backreference  \1 .. \9  \k<name>
//
// Nothing here needs floating point: all counters, bounds and scores are plain
// integers, and where a fraction is required it is expressed in per-mille.
namespace regex;

// --- Character constants -------------------------------------------------
//
// The lexer and the class parser compare raw characters on almost every line,
// so each significant character is given a name once and referred to by that
// name afterwards.  Spelling them out at every use site would make the
// character-level logic below far harder to read.

let CHAR_NUL: char = '\0';
let CHAR_TAB: char = '\t';
let CHAR_NEWLINE: char = '\n';
let CHAR_RETURN: char = '\r';
let CHAR_FORMFEED: char = '\u{0C}';
let CHAR_BACKSPACE: char = '\u{08}';
let CHAR_SPACE: char = ' ';

// Metacharacters.
let CHAR_BACKSLASH: char = '\\';
let CHAR_CARET: char = '^';
let CHAR_DOLLAR: char = '$';
let CHAR_DOT: char = '.';
let CHAR_STAR: char = '*';
let CHAR_PLUS: char = '+';
let CHAR_QUESTION: char = '?';
let CHAR_PIPE: char = '|';
let CHAR_LPAREN: char = '(';
let CHAR_RPAREN: char = ')';
let CHAR_LBRACKET: char = '[';
let CHAR_RBRACKET: char = ']';
let CHAR_LBRACE: char = '{';
let CHAR_RBRACE: char = '}';
let CHAR_COMMA: char = ',';
let CHAR_MINUS: char = '-';
let CHAR_COLON: char = ':';
let CHAR_LESS: char = '<';
let CHAR_GREATER: char = '>';
let CHAR_EQUALS: char = '=';
let CHAR_BANG: char = '!';
let CHAR_UNDERSCORE: char = '_';
let CHAR_SLASH: char = '/';
let CHAR_QUOTE: char = '"';

// Digits and the letter ranges, used by the classification helpers.
let CHAR_ZERO: char = '0';
let CHAR_ONE: char = '1';
let CHAR_SEVEN: char = '7';
let CHAR_NINE: char = '9';
let CHAR_LOWER_A: char = 'a';
let CHAR_LOWER_B: char = 'b';
let CHAR_LOWER_D: char = 'd';
let CHAR_LOWER_E: char = 'e';
let CHAR_LOWER_F: char = 'f';
let CHAR_LOWER_H: char = 'h';
let CHAR_LOWER_K: char = 'k';
let CHAR_LOWER_N: char = 'n';
let CHAR_LOWER_R: char = 'r';
let CHAR_LOWER_S: char = 's';
let CHAR_LOWER_T: char = 't';
let CHAR_LOWER_U: char = 'u';
let CHAR_LOWER_V: char = 'v';
let CHAR_LOWER_W: char = 'w';
let CHAR_LOWER_X: char = 'x';
let CHAR_LOWER_Z: char = 'z';
let CHAR_UPPER_A: char = 'A';
let CHAR_UPPER_B: char = 'B';
let CHAR_UPPER_D: char = 'D';
let CHAR_UPPER_F: char = 'F';
let CHAR_UPPER_S: char = 'S';
let CHAR_UPPER_W: char = 'W';
let CHAR_UPPER_Z: char = 'Z';

// The apostrophe cannot be written as a character escape in this dialect, so
// it is kept as a one character string and read out of it when needed.
let TEXT_APOSTROPHE: string = "'";

fn apostrophe() -> char {
    return TEXT_APOSTROPHE.at(0);
}

// --- Numeric limits ------------------------------------------------------

// Character codes are treated as a byte alphabet.
let ALPHABET_SIZE: int = 256;
let CODE_MIN: int = 0;
let CODE_MAX: int = 255;

// Sentinel used wherever "no such index" must be expressed.
let NONE: int = -1;

// The largest repetition count a `{n,m}` bound may name.  Larger bounds are
// rejected by the parser rather than expanded, because expansion is linear in
// the bound and a hostile pattern could otherwise blow up the program.
let MAX_REPEAT: int = 1000;

// Upper bound on the number of nested groups the recursive descent parser
// will accept.  This keeps the recursion depth, and therefore the native
// stack usage, bounded for arbitrary input.
let MAX_GROUP_DEPTH: int = 64;

// Capture groups are numbered from 1; group 0 is the whole match.
let MAX_CAPTURES: int = 32;

// Budget for the backtracking matcher, counted in step units.  A pathological
// pattern such as (a+)+$ against a long run of a's will exhaust it and the
// matcher reports MatchStatus::Exhausted instead of running forever.
let BACKTRACK_BUDGET: int = 1000000;

// The optimiser gives up on a pass once it has rewritten this many
// instructions, so compilation stays linear even for very large programs.
let OPTIMISER_BUDGET: int = 4096;

// --- Text constants ------------------------------------------------------

let TEXT_EMPTY: string = "";
let TEXT_DOT: string = ".";
let TEXT_STAR: string = "*";
let TEXT_PLUS: string = "+";
let TEXT_QUESTION: string = "?";
let TEXT_PIPE: string = "|";
let TEXT_CARET: string = "^";
let TEXT_DOLLAR: string = "$";
let TEXT_LPAREN: string = "(";
let TEXT_RPAREN: string = ")";
let TEXT_LBRACKET: string = "[";
let TEXT_RBRACKET: string = "]";
let TEXT_LBRACE: string = "{";
let TEXT_RBRACE: string = "}";
let TEXT_COMMA: string = ",";
let TEXT_MINUS: string = "-";
let TEXT_LAZY_SUFFIX: string = "?";

// Escape spellings.  The doubled backslash in the source produces a single
// real backslash in the value.
let ESCAPE_DIGIT: string = "\\d";
let ESCAPE_NOT_DIGIT: string = "\\D";
let ESCAPE_WORD: string = "\\w";
let ESCAPE_NOT_WORD: string = "\\W";
let ESCAPE_SPACE: string = "\\s";
let ESCAPE_NOT_SPACE: string = "\\S";
let ESCAPE_HORIZONTAL: string = "\\h";
let ESCAPE_VERTICAL: string = "\\v";
let ESCAPE_WORD_BOUNDARY: string = "\\b";
let ESCAPE_NOT_WORD_BOUNDARY: string = "\\B";
let ESCAPE_TEXT_START: string = "\\A";
let ESCAPE_TEXT_END: string = "\\z";
let ESCAPE_TAB: string = "\\t";
let ESCAPE_NEWLINE: string = "\\n";
let ESCAPE_RETURN: string = "\\r";
let ESCAPE_FORMFEED: string = "\\f";
let ESCAPE_BACKSLASH: string = "\\\\";

// POSIX style class names, spelled without their bracket-colon wrapper.
let POSIX_ALPHA: string = "alpha";
let POSIX_DIGIT: string = "digit";
let POSIX_ALNUM: string = "alnum";
let POSIX_UPPER: string = "upper";
let POSIX_LOWER: string = "lower";
let POSIX_SPACE: string = "space";
let POSIX_BLANK: string = "blank";
let POSIX_PUNCT: string = "punct";
let POSIX_PRINT: string = "print";
let POSIX_GRAPH: string = "graph";
let POSIX_CNTRL: string = "cntrl";
let POSIX_XDIGIT: string = "xdigit";
let POSIX_WORD: string = "word";

// Diagnostic message texts.
let ERROR_NONE: string = "";
let ERROR_UNEXPECTED_END: string = "unexpected end of pattern";
let ERROR_UNBALANCED_PAREN: string = "unbalanced parenthesis";
let ERROR_UNCLOSED_CLASS: string = "unterminated character class";
let ERROR_BAD_RANGE: string = "character class range is reversed";
let ERROR_BAD_REPEAT: string = "repetition bound is malformed";
let ERROR_REPEAT_TOO_LARGE: string = "repetition bound exceeds the limit";
let ERROR_NOTHING_TO_REPEAT: string = "nothing to repeat";
let ERROR_TRAILING_BACKSLASH: string = "pattern ends with a backslash";
let ERROR_BAD_ESCAPE: string = "unrecognised escape sequence";
let ERROR_BAD_GROUP: string = "malformed group opening";
let ERROR_DEPTH_EXCEEDED: string = "group nesting is too deep";
let ERROR_BAD_BACKREF: string = "backreference names an unknown group";
let ERROR_UNKNOWN_CLASS: string = "unknown POSIX class name";

// --- Character classification -------------------------------------------
//
// Everything downstream of the lexer asks these questions constantly, so they
// live as free functions rather than as methods on some helper object.  They
// are written as explicit range tests instead of table lookups because the
// alphabet is small and the branch layout is what the compiler benchmark is
// actually meant to measure.

fn is_digit(c: char) -> bool {
    if (c < CHAR_ZERO) {
        return false;
    }
    if (c > CHAR_NINE) {
        return false;
    }
    return true;
}

fn is_octal_digit(c: char) -> bool {
    if (c < CHAR_ZERO) {
        return false;
    }
    if (c > CHAR_SEVEN) {
        return false;
    }
    return true;
}

fn is_lower(c: char) -> bool {
    if (c < CHAR_LOWER_A) {
        return false;
    }
    if (c > CHAR_LOWER_Z) {
        return false;
    }
    return true;
}

fn is_upper(c: char) -> bool {
    if (c < CHAR_UPPER_A) {
        return false;
    }
    if (c > CHAR_UPPER_Z) {
        return false;
    }
    return true;
}

fn is_alpha(c: char) -> bool {
    if (is_lower(c)) {
        return true;
    }
    if (is_upper(c)) {
        return true;
    }
    return false;
}

fn is_alnum(c: char) -> bool {
    if (is_alpha(c)) {
        return true;
    }
    return is_digit(c);
}

// The word alphabet is the one \w selects: letters, digits and underscore.
fn is_word_char(c: char) -> bool {
    if (is_alnum(c)) {
        return true;
    }
    if (c == CHAR_UNDERSCORE) {
        return true;
    }
    return false;
}

fn is_hex_digit(c: char) -> bool {
    if (is_digit(c)) {
        return true;
    }
    if (c >= CHAR_LOWER_A and c <= CHAR_LOWER_F) {
        return true;
    }
    if (c >= CHAR_UPPER_A and c <= CHAR_UPPER_F) {
        return true;
    }
    return false;
}

// Horizontal whitespace only: space and tab.
fn is_horizontal_space(c: char) -> bool {
    if (c == CHAR_SPACE) {
        return true;
    }
    if (c == CHAR_TAB) {
        return true;
    }
    return false;
}

// Vertical whitespace: the line terminators plus form feed.
fn is_vertical_space(c: char) -> bool {
    if (c == CHAR_NEWLINE) {
        return true;
    }
    if (c == CHAR_RETURN) {
        return true;
    }
    if (c == CHAR_FORMFEED) {
        return true;
    }
    return false;
}

fn is_space_char(c: char) -> bool {
    if (is_horizontal_space(c)) {
        return true;
    }
    if (is_vertical_space(c)) {
        return true;
    }
    return false;
}

// Control characters are everything below space, plus the delete code.
fn is_control(c: char) -> bool {
    let code = char_code(c);
    if (code < 32) {
        return true;
    }
    if (code == 127) {
        return true;
    }
    return false;
}

fn is_printable(c: char) -> bool {
    let code = char_code(c);
    if (code < 32) {
        return false;
    }
    if (code > 126) {
        return false;
    }
    return true;
}

// Graphic characters are the printable ones minus the space itself.
fn is_graphic(c: char) -> bool {
    if (not is_printable(c)) {
        return false;
    }
    return c != CHAR_SPACE;
}

fn is_punctuation(c: char) -> bool {
    if (not is_graphic(c)) {
        return false;
    }
    if (is_alnum(c)) {
        return false;
    }
    return true;
}

// The characters that carry a special meaning outside a bracket expression.
fn is_metacharacter(c: char) -> bool {
    if (c == CHAR_BACKSLASH) {
        return true;
    }
    if (c == CHAR_CARET) {
        return true;
    }
    if (c == CHAR_DOLLAR) {
        return true;
    }
    if (c == CHAR_DOT) {
        return true;
    }
    if (c == CHAR_STAR) {
        return true;
    }
    if (c == CHAR_PLUS) {
        return true;
    }
    if (c == CHAR_QUESTION) {
        return true;
    }
    if (c == CHAR_PIPE) {
        return true;
    }
    if (c == CHAR_LPAREN) {
        return true;
    }
    if (c == CHAR_RPAREN) {
        return true;
    }
    if (c == CHAR_LBRACKET) {
        return true;
    }
    if (c == CHAR_RBRACKET) {
        return true;
    }
    if (c == CHAR_LBRACE) {
        return true;
    }
    if (c == CHAR_RBRACE) {
        return true;
    }
    return false;
}

// Inside a bracket expression only a handful of characters keep a meaning.
fn is_class_metacharacter(c: char) -> bool {
    if (c == CHAR_BACKSLASH) {
        return true;
    }
    if (c == CHAR_RBRACKET) {
        return true;
    }
    if (c == CHAR_MINUS) {
        return true;
    }
    if (c == CHAR_CARET) {
        return true;
    }
    if (c == CHAR_LBRACKET) {
        return true;
    }
    return false;
}

// --- Case and code conversions ------------------------------------------

fn to_lower(c: char) -> char {
    if (is_upper(c)) {
        return code_char(char_code(c) + 32);
    }
    return c;
}

fn to_upper(c: char) -> char {
    if (is_lower(c)) {
        return code_char(char_code(c) - 32);
    }
    return c;
}

// Numeric value of a hexadecimal digit, or NONE when the character is not one.
fn hex_value(c: char) -> int {
    if (is_digit(c)) {
        return char_code(c) - char_code(CHAR_ZERO);
    }
    if (c >= CHAR_LOWER_A and c <= CHAR_LOWER_F) {
        return 10 + char_code(c) - char_code(CHAR_LOWER_A);
    }
    if (c >= CHAR_UPPER_A and c <= CHAR_UPPER_F) {
        return 10 + char_code(c) - char_code(CHAR_UPPER_A);
    }
    return NONE;
}

fn digit_value(c: char) -> int {
    if (not is_digit(c)) {
        return NONE;
    }
    return char_code(c) - char_code(CHAR_ZERO);
}

// The two bridges between the character and integer views of the alphabet.
fn char_code(c: char) -> int {
    var code: int = 0;
    code = c;
    return code;
}

fn code_char(code: int) -> char {
    var c: char = CHAR_NUL;
    c = code;
    return c;
}

// Case-insensitive comparison of two characters, used when the
// MatchFlags::IgnoreCase flag is set on a compiled program.
fn same_char_folded(a: char, b: char) -> bool {
    if (a == b) {
        return true;
    }
    return to_lower(a) == to_lower(b);
}

// --- Pattern tokens ------------------------------------------------------
//
// The lexer turns the raw pattern text into a token stream.  It deliberately
// does *not* understand bracket expressions: a `[` produces a single
// ClassOpen token and the parser then drives the lexer character by character
// until the matching `]`.  Bracket expressions have their own tiny grammar in
// which almost nothing is a metacharacter, and folding it into the main token
// rules would make both harder to follow.

choice TokenKind {
    EndOfPattern,
    Literal,            // an ordinary character, or an escaped one
    AnyChar,            // .
    AlternationBar,     // |
    GroupOpen,          // ( or (?: or (?<name>
    GroupClose,         // )
    ClassOpen,          // [ or [^
    ClassClose,         // ]
    Star,               // *
    Plus,               // +
    Question,           // ?
    RepeatOpen,         // {n} {n,} {n,m}
    CaretAnchor,        // ^
    DollarAnchor,       // $
    TextStartAnchor,    // \A
    TextEndAnchor,      // \z
    WordBoundary,       // \u{08}
    NotWordBoundary,    // \B
    ShorthandClass,     // \d \D \w \W \s \S \h \u{0B}
    Backreference,      // \1 .. \9 or \k<name>
    Invalid,
}

// Which shorthand a ShorthandClass token stands for.
choice Shorthand {
    ShortDigit,
    ShortNotDigit,
    ShortWord,
    ShortNotWord,
    ShortSpace,
    ShortNotSpace,
    ShortHorizontal,
    ShortVertical,
    ShortNone,
}

// How a group opening was spelled.
choice GroupKind {
    Capturing,          // (
    NonCapturing,       // (?:
    Named,              // (?<name>
    Atomic,         // (?>
}

class Token {

    fn is_end(self) -> bool {
        return self.kind == TokenKind.EndOfPattern;
    }

    fn is_literal(self) -> bool {
        return self.kind == TokenKind.Literal;
    }

    // Quantifiers all bind to the preceding atom, so the parser asks this
    // question rather than testing three kinds at every call site.
    fn is_quantifier(self) -> bool {
        if (self.kind == TokenKind.Star) {
            return true;
        }
        if (self.kind == TokenKind.Plus) {
            return true;
        }
        if (self.kind == TokenKind.Question) {
            return true;
        }
        if (self.kind == TokenKind.RepeatOpen) {
            return true;
        }
        return false;
    }

    fn is_anchor(self) -> bool {
        if (self.kind == TokenKind.CaretAnchor) {
            return true;
        }
        if (self.kind == TokenKind.DollarAnchor) {
            return true;
        }
        if (self.kind == TokenKind.TextStartAnchor) {
            return true;
        }
        if (self.kind == TokenKind.TextEndAnchor) {
            return true;
        }
        if (self.kind == TokenKind.WordBoundary) {
            return true;
        }
        if (self.kind == TokenKind.NotWordBoundary) {
            return true;
        }
        return false;
    }

    // True when the token can begin an atom.  Used for error recovery and by
    // the optimiser when it scans for a literal prefix.
    fn starts_atom(self) -> bool {
        if (self.is_literal()) {
            return true;
        }
        if (self.kind == TokenKind.AnyChar) {
            return true;
        }
        if (self.kind == TokenKind.ClassOpen) {
            return true;
        }
        if (self.kind == TokenKind.GroupOpen) {
            return true;
        }
        if (self.kind == TokenKind.ShorthandClass) {
            return true;
        }
        if (self.kind == TokenKind.Backreference) {
            return true;
        }
        return self.is_anchor();
    }

    var kind: TokenKind = TokenKind.Invalid;
    var value: char = '\0';
    var text: string;
    var shorthand: Shorthand = Shorthand.ShortNone;
    var group_kind: GroupKind = GroupKind.Capturing;
    var lower: int = 0;
    var upper: int = -1;
    var position: int = 0;
    // Set when the character came from an escape, so `\*` is a literal star
    // rather than a quantifier even though the value is identical.
    var escaped: bool = false;
}

// --- Pattern lexer -------------------------------------------------------

class PatternLexer {

    fn at_end(self) -> bool {
        return self.pos >= self.source.size();
    }

    fn remaining(self) -> int {
        return self.source.size() - self.pos;
    }

    fn peek(self) -> char {
        if (self.at_end()) {
            return CHAR_NUL;
        }
        return self.source.at(self.pos);
    }

    fn peek_at(self, offset: int) -> char {
        let index = self.pos + offset;
        if (index < 0) {
            return CHAR_NUL;
        }
        if (index >= self.source.size()) {
            return CHAR_NUL;
        }
        return self.source.at(index);
    }

    fn advance(ref self) -> char {
        if (self.at_end()) {
            return CHAR_NUL;
        }
        let c = self.source.at(self.pos);
        self.pos += 1;
        return c;
    }

    fn accept(ref self, expected: char) -> bool {
        if (self.peek() != expected) {
            return false;
        }
        self.pos += 1;
        return true;
    }

    fn fail(ref self, message: string) {
        if (self.ok) {
            self.ok = false;
            self.error = message;
            self.error_position = self.pos;
        }
    }

    // --- Token construction ----------------------------------------------

    fn make_token(self, kind: TokenKind, at: int) -> Token {
        var t = Token({.kind = kind, .position = at});
        return t;
    }

    fn make_literal(self, c: char, at: int, escaped: bool) -> Token {
        var t = Token({.kind = TokenKind.Literal, .value = c, .position = at});
        t.escaped = escaped;
        return t;
    }

    fn make_shorthand(self, which: Shorthand, at: int) -> Token {
        var t = Token({.kind = TokenKind.ShorthandClass, .position = at});
        t.shorthand = which;
        return t;
    }

    // --- Driving loop -----------------------------------------------------

    fn tokenize(ref self) -> bool {
        self.tokens.clear();
        self.pos = 0;
        self.ok = true;
        self.error = ERROR_NONE;
        while (not self.at_end()) {
            let t = self.next_token();
            if (not self.ok) {
                return false;
            }
            self.tokens.push_back(t);
        }
        self.tokens.push_back(self.make_token(TokenKind.EndOfPattern, self.source.size()));
        return true;
    }

    // One token.  The big branch tree below is the heart of the lexer: each
    // metacharacter is checked in turn and anything that survives is an
    // ordinary literal.
    fn next_token(ref self) -> Token {
        let at = self.pos;
        let c = self.advance();
        if (c == CHAR_BACKSLASH) {
            return self.lex_escape(at);
        }
        if (c == CHAR_DOT) {
            return self.make_token(TokenKind.AnyChar, at);
        }
        if (c == CHAR_PIPE) {
            return self.make_token(TokenKind.AlternationBar, at);
        }
        if (c == CHAR_LPAREN) {
            return self.lex_group_open(at);
        }
        if (c == CHAR_RPAREN) {
            return self.make_token(TokenKind.GroupClose, at);
        }
        if (c == CHAR_LBRACKET) {
            return self.make_token(TokenKind.ClassOpen, at);
        }
        if (c == CHAR_RBRACKET) {
            // A stray `]` outside a bracket expression is an ordinary
            // character; only the class parser gives it a meaning.
            return self.make_literal(c, at, false);
        }
        if (c == CHAR_STAR) {
            return self.make_token(TokenKind.Star, at);
        }
        if (c == CHAR_PLUS) {
            return self.make_token(TokenKind.Plus, at);
        }
        if (c == CHAR_QUESTION) {
            return self.make_token(TokenKind.Question, at);
        }
        if (c == CHAR_LBRACE) {
            return self.lex_repeat_bound(at);
        }
        if (c == CHAR_RBRACE) {
            return self.make_literal(c, at, false);
        }
        if (c == CHAR_CARET) {
            return self.make_token(TokenKind.CaretAnchor, at);
        }
        if (c == CHAR_DOLLAR) {
            return self.make_token(TokenKind.DollarAnchor, at);
        }
        return self.make_literal(c, at, false);
    }

    // --- Escapes ----------------------------------------------------------
    //
    // Called with the backslash already consumed.  Escapes fall into four
    // families: shorthand classes, anchors, backreferences and single
    // character substitutions.

    fn lex_escape(ref self, at: int) -> Token {
        if (self.at_end()) {
            self.fail(ERROR_TRAILING_BACKSLASH);
            return self.make_token(TokenKind.Invalid, at);
        }
        let c = self.advance();
        if (c == CHAR_LOWER_D) {
            return self.make_shorthand(Shorthand.ShortDigit, at);
        }
        if (c == CHAR_UPPER_D) {
            return self.make_shorthand(Shorthand.ShortNotDigit, at);
        }
        if (c == CHAR_LOWER_W) {
            return self.make_shorthand(Shorthand.ShortWord, at);
        }
        if (c == CHAR_UPPER_W) {
            return self.make_shorthand(Shorthand.ShortNotWord, at);
        }
        if (c == CHAR_LOWER_S) {
            return self.make_shorthand(Shorthand.ShortSpace, at);
        }
        if (c == CHAR_UPPER_S) {
            return self.make_shorthand(Shorthand.ShortNotSpace, at);
        }
        if (c == CHAR_LOWER_H) {
            return self.make_shorthand(Shorthand.ShortHorizontal, at);
        }
        if (c == CHAR_LOWER_V) {
            return self.make_shorthand(Shorthand.ShortVertical, at);
        }
        if (c == CHAR_LOWER_B) {
            return self.make_token(TokenKind.WordBoundary, at);
        }
        if (c == CHAR_UPPER_B) {
            return self.make_token(TokenKind.NotWordBoundary, at);
        }
        if (c == CHAR_UPPER_A) {
            return self.make_token(TokenKind.TextStartAnchor, at);
        }
        if (c == CHAR_LOWER_Z) {
            return self.make_token(TokenKind.TextEndAnchor, at);
        }
        if (c == CHAR_LOWER_K) {
            return self.lex_named_backreference(at);
        }
        if (is_digit(c) and c != CHAR_ZERO) {
            return self.lex_numeric_backreference(c, at);
        }
        if (c == CHAR_LOWER_X) {
            return self.lex_hex_escape(at);
        }
        return self.make_literal(decode_escape(c), at, true);
    }

    // \k<name> refers to a named group.  The name is stored on the token and
    // resolved to an index by the parser, which owns the name table.
    fn lex_named_backreference(ref self, at: int) -> Token {
        var t = self.make_token(TokenKind.Backreference, at);
        if (not self.accept(CHAR_LESS)) {
            self.fail(ERROR_BAD_BACKREF);
            return self.make_token(TokenKind.Invalid, at);
        }
        var name = TEXT_EMPTY;
        while (not self.at_end()) {
            let c = self.peek();
            if (c == CHAR_GREATER) {
                break;
            }
            if (not is_word_char(c)) {
                self.fail(ERROR_BAD_BACKREF);
                return self.make_token(TokenKind.Invalid, at);
            }
            name.push_back(c);
            self.pos += 1;
        }
        if (not self.accept(CHAR_GREATER)) {
            self.fail(ERROR_BAD_BACKREF);
            return self.make_token(TokenKind.Invalid, at);
        }
        if (name.empty()) {
            self.fail(ERROR_BAD_BACKREF);
            return self.make_token(TokenKind.Invalid, at);
        }
        t.text = name;
        t.lower = NONE;
        return t;
    }

    // \1 .. \9, and multi-digit forms such as \12 when that many groups are
    // plausible.  The parser rejects references past the group count.
    fn lex_numeric_backreference(ref self, first: char, at: int) -> Token {
        var value = digit_value(first);
        while (not self.at_end()) {
            let c = self.peek();
            if (not is_digit(c)) {
                break;
            }
            let next = value * 10 + digit_value(c);
            if (next >= MAX_CAPTURES) {
                break;
            }
            value = next;
            self.pos += 1;
        }
        var t = self.make_token(TokenKind.Backreference, at);
        t.lower = value;
        return t;
    }

    // \xNN.  Exactly two hexadecimal digits; anything else is an error rather
    // than a silent fallback, because a typo there is almost never intended.
    fn lex_hex_escape(ref self, at: int) -> Token {
        if (self.remaining() < 2) {
            self.fail(ERROR_BAD_ESCAPE);
            return self.make_token(TokenKind.Invalid, at);
        }
        let high = self.advance();
        let low = self.advance();
        if (not is_hex_digit(high)) {
            self.fail(ERROR_BAD_ESCAPE);
            return self.make_token(TokenKind.Invalid, at);
        }
        if (not is_hex_digit(low)) {
            self.fail(ERROR_BAD_ESCAPE);
            return self.make_token(TokenKind.Invalid, at);
        }
        let code = hex_value(high) * 16 + hex_value(low);
        return self.make_literal(code_char(code), at, true);
    }

    // --- Group openings ---------------------------------------------------

    fn lex_group_open(ref self, at: int) -> Token {
        var t = self.make_token(TokenKind.GroupOpen, at);
        if (self.peek() != CHAR_QUESTION) {
            t.group_kind = GroupKind.Capturing;
            return t;
        }
        self.pos += 1;
        let c = self.advance();
        if (c == CHAR_COLON) {
            t.group_kind = GroupKind.NonCapturing;
            return t;
        }
        if (c == CHAR_GREATER) {
            t.group_kind = GroupKind.Atomic;
            return t;
        }
        if (c == CHAR_LESS) {
            var name = TEXT_EMPTY;
            while (not self.at_end()) {
                let n = self.peek();
                if (n == CHAR_GREATER) {
                    break;
                }
                if (not is_word_char(n)) {
                    self.fail(ERROR_BAD_GROUP);
                    return self.make_token(TokenKind.Invalid, at);
                }
                name.push_back(n);
                self.pos += 1;
            }
            if (not self.accept(CHAR_GREATER)) {
                self.fail(ERROR_BAD_GROUP);
                return self.make_token(TokenKind.Invalid, at);
            }
            if (name.empty()) {
                self.fail(ERROR_BAD_GROUP);
                return self.make_token(TokenKind.Invalid, at);
            }
            t.group_kind = GroupKind.Named;
            t.text = name;
            return t;
        }
        self.fail(ERROR_BAD_GROUP);
        return self.make_token(TokenKind.Invalid, at);
    }

    // --- Repetition bounds ------------------------------------------------
    //
    // Called with the `{` consumed.  A brace that does not open a well formed
    // bound is treated as a literal brace, which is what every practical
    // engine does; only a bound that parses but is out of range is an error.

    fn lex_repeat_bound(ref self, at: int) -> Token {
        let save = self.pos;
        if (not is_digit(self.peek())) {
            self.pos = save;
            return self.make_literal(CHAR_LBRACE, at, false);
        }
        var lower: int = 0;
        var digits: int = 0;
        while (is_digit(self.peek())) {
            lower = lower * 10 + digit_value(self.peek());
            digits += 1;
            self.pos += 1;
            if (lower > MAX_REPEAT) {
                self.fail(ERROR_REPEAT_TOO_LARGE);
                return self.make_token(TokenKind.Invalid, at);
            }
        }
        var t = self.make_token(TokenKind.RepeatOpen, at);
        t.lower = lower;
        if (self.accept(CHAR_RBRACE)) {
            t.upper = lower;
            return t;
        }
        if (not self.accept(CHAR_COMMA)) {
            self.pos = save;
            return self.make_literal(CHAR_LBRACE, at, false);
        }
        if (self.accept(CHAR_RBRACE)) {
            // `{n,}` is an open ended bound.
            t.upper = NONE;
            return t;
        }
        if (not is_digit(self.peek())) {
            self.pos = save;
            return self.make_literal(CHAR_LBRACE, at, false);
        }
        var upper: int = 0;
        while (is_digit(self.peek())) {
            upper = upper * 10 + digit_value(self.peek());
            self.pos += 1;
            if (upper > MAX_REPEAT) {
                self.fail(ERROR_REPEAT_TOO_LARGE);
                return self.make_token(TokenKind.Invalid, at);
            }
        }
        if (not self.accept(CHAR_RBRACE)) {
            self.pos = save;
            return self.make_literal(CHAR_LBRACE, at, false);
        }
        if (upper < lower) {
            self.fail(ERROR_BAD_REPEAT);
            return self.make_token(TokenKind.Invalid, at);
        }
        t.upper = upper;
        return t;
    }

    var source: string;
    var tokens: vector(Token);
    var pos: int = 0;
    var error: string;
    var error_position: int = 0;
    var ok: bool = true;
}

// Single character escapes that stand for a control character.  Anything not
// listed keeps its own value, so `\+` is a plus and `\/` is a slash.
fn decode_escape(marker: char) -> char {
    if (marker == CHAR_LOWER_N) {
        return CHAR_NEWLINE;
    }
    if (marker == CHAR_LOWER_T) {
        return CHAR_TAB;
    }
    if (marker == CHAR_LOWER_R) {
        return CHAR_RETURN;
    }
    if (marker == CHAR_LOWER_F) {
        return CHAR_FORMFEED;
    }
    if (marker == CHAR_ZERO) {
        return CHAR_NUL;
    }
    if (marker == CHAR_LOWER_E) {
        return code_char(27);
    }
    return marker;
}

// The inverse mapping, used when a pattern is printed back out.
fn escape_spelling(c: char) -> string {
    if (c == CHAR_NEWLINE) {
        return ESCAPE_NEWLINE;
    }
    if (c == CHAR_TAB) {
        return ESCAPE_TAB;
    }
    if (c == CHAR_RETURN) {
        return ESCAPE_RETURN;
    }
    if (c == CHAR_FORMFEED) {
        return ESCAPE_FORMFEED;
    }
    if (c == CHAR_BACKSLASH) {
        return ESCAPE_BACKSLASH;
    }
    var out = TEXT_EMPTY;
    if (is_metacharacter(c)) {
        out.push_back(CHAR_BACKSLASH);
    }
    out.push_back(c);
    return out;
}

// --- Character classes ---------------------------------------------------
//
// A class is stored as a sorted list of inclusive code ranges plus a negation
// flag.  Ranges are kept normalised - sorted by low bound, non-overlapping and
// non-adjacent - which makes membership a short scan and lets the optimiser
// merge two classes by concatenating and re-normalising.

class ClassRange {

    fn contains(self, code: int) -> bool {
        if (code < self.low) {
            return false;
        }
        if (code > self.high) {
            return false;
        }
        return true;
    }

    fn width(self) -> int {
        return self.high - self.low + 1;
    }

    fn is_single(self) -> bool {
        return self.low == self.high;
    }

    // Two ranges may be fused when they overlap or merely touch.
    fn touches(self, other: ClassRange) -> bool {
        if (other.low > self.high + 1) {
            return false;
        }
        if (self.low > other.high + 1) {
            return false;
        }
        return true;
    }

    fn fuse(ref self, other: ClassRange) {
        if (other.low < self.low) {
            self.low = other.low;
        }
        if (other.high > self.high) {
            self.high = other.high;
        }
    }

    var low: int = 0;
    var high: int = 0;
}

fn make_range(low: int, high: int) -> ClassRange {
    var r = ClassRange({.low = low, .high = high});
    if (high < low) {
        r.low = high;
        r.high = low;
    }
    return r;
}

fn single_range(c: char) -> ClassRange {
    let code = char_code(c);
    return make_range(code, code);
}

class CharClass {

    fn empty(self) -> bool {
        return self.ranges.empty();
    }

    fn range_count(self) -> int {
        return self.ranges.size();
    }

    fn add_range(ref self, low: int, high: int) {
        self.ranges.push_back(make_range(low, high));
        self.normalised = false;
    }

    fn add_char(ref self, c: char) {
        let code = char_code(c);
        self.add_range(code, code);
    }

    fn add_char_range(ref self, low: char, high: char) {
        self.add_range(char_code(low), char_code(high));
    }

    // Pull every range of `other` in.  Used both by the class parser, when it
    // expands a shorthand inside brackets, and by the optimiser when it merges
    // sibling classes under an alternation.
    fn add_class(ref self, other: CharClass) {
        for (r in other.ranges) {
            self.add_range(r.low, r.high);
        }
        self.normalised = false;
    }

    fn negate(ref self) {
        self.negated = not self.negated;
    }

    // Insertion sort over the range list.  Classes are tiny - a dozen ranges
    // at the very worst - so the quadratic bound never matters and the code
    // stays obvious.
    fn sort_ranges(ref self) {
        var i: int = 1;
        while (i < self.ranges.size()) {
            var j = i;
            while (j > 0) {
                let ref left: auto = self.ranges[j - 1];
                let ref right: auto = self.ranges[j];
                if (left.low <= right.low) {
                    break;
                }
                swap(self.ranges[j - 1], self.ranges[j]);
                j -= 1;
            }
            i += 1;
        }
    }

    // Sort, then fuse every pair that touches.  Afterwards the list is a
    // canonical description of the set, so two classes are equal exactly when
    // their range lists are.
    fn normalise(ref self) {
        if (self.normalised) {
            return;
        }
        if (self.ranges.empty()) {
            self.normalised = true;
            return;
        }
        self.sort_ranges();
        var merged = vector(ClassRange)();
        merged.push_back(self.ranges[0]);
        var i: int = 1;
        while (i < self.ranges.size()) {
            let ref next: auto = self.ranges[i];
            let ref last: auto = merged.back();
            if (last.touches(next)) {
                last.fuse(next);
            } else {
                merged.push_back(next);
            }
            i += 1;
        }
        self.ranges = move(merged);
        self.normalised = true;
    }

    fn contains_code(self, code: int) -> bool {
        var found = false;
        for (r in self.ranges) {
            if (r.contains(code)) {
                found = true;
                break;
            }
        }
        if (self.negated) {
            return not found;
        }
        return found;
    }

    fn matches(self, c: char) -> bool {
        return self.contains_code(char_code(c));
    }

    // Case-insensitive membership: try the character as written and then its
    // opposite case.  Folding the class itself would be faster but would lose
    // the original spelling, which the printer wants.
    fn matches_folded(self, c: char) -> bool {
        if (self.matches(c)) {
            return true;
        }
        if (is_lower(c)) {
            return self.matches(to_upper(c));
        }
        if (is_upper(c)) {
            return self.matches(to_lower(c));
        }
        return false;
    }

    // How many distinct codes the class accepts.  The optimiser uses this to
    // decide whether a class is worth turning into a single-character test.
    fn cardinality(self) -> int {
        var total: int = 0;
        for (r in self.ranges) {
            total += r.width();
        }
        if (self.negated) {
            return ALPHABET_SIZE - total;
        }
        return total;
    }

    fn is_single_char(self) -> bool {
        if (self.negated) {
            return false;
        }
        if (self.ranges.size() != 1) {
            return false;
        }
        let ref only: auto = self.ranges[0];
        return only.is_single();
    }

    fn single_char(self) -> char {
        let ref only: auto = self.ranges[0];
        return code_char(only.low);
    }

    fn equals(self, other: CharClass) -> bool {
        if (self.negated != other.negated) {
            return false;
        }
        if (self.ranges.size() != other.ranges.size()) {
            return false;
        }
        var i: int = 0;
        while (i < self.ranges.size()) {
            let ref a: auto = self.ranges[i];
            let ref b: auto = other.ranges[i];
            if (a.low != b.low) {
                return false;
            }
            if (a.high != b.high) {
                return false;
            }
            i += 1;
        }
        return true;
    }

    // Render the class back into bracket notation.
    fn spell(self) -> string {
        var out = TEXT_LBRACKET;
        if (self.negated) {
            out.push_back(CHAR_CARET);
        }
        for (r in self.ranges) {
            out.append(escape_spelling(code_char(r.low)));
            if (not r.is_single()) {
                out.push_back(CHAR_MINUS);
                out.append(escape_spelling(code_char(r.high)));
            }
        }
        out.append(TEXT_RBRACKET);
        return out;
    }

    var ranges: vector(ClassRange);
    var negated: bool = false;
    var normalised: bool = true;
}

// --- Shorthand and POSIX class tables ------------------------------------
//
// Both families are built from the same primitive: fill a class with ranges
// and optionally flip its negation flag.

fn class_of_digits() -> CharClass {
    var c = CharClass();
    c.add_char_range(CHAR_ZERO, CHAR_NINE);
    return c;
}

fn class_of_word() -> CharClass {
    var c = CharClass();
    c.add_char_range(CHAR_LOWER_A, CHAR_LOWER_Z);
    c.add_char_range(CHAR_UPPER_A, CHAR_UPPER_Z);
    c.add_char_range(CHAR_ZERO, CHAR_NINE);
    c.add_char(CHAR_UNDERSCORE);
    return c;
}

fn class_of_space() -> CharClass {
    var c = CharClass();
    c.add_char(CHAR_SPACE);
    c.add_char(CHAR_TAB);
    c.add_char(CHAR_NEWLINE);
    c.add_char(CHAR_RETURN);
    c.add_char(CHAR_FORMFEED);
    c.add_char(code_char(11));
    return c;
}

fn class_of_horizontal() -> CharClass {
    var c = CharClass();
    c.add_char(CHAR_SPACE);
    c.add_char(CHAR_TAB);
    return c;
}

fn class_of_vertical() -> CharClass {
    var c = CharClass();
    c.add_char(CHAR_NEWLINE);
    c.add_char(CHAR_RETURN);
    c.add_char(CHAR_FORMFEED);
    c.add_char(code_char(11));
    return c;
}

fn class_of_alpha() -> CharClass {
    var c = CharClass();
    c.add_char_range(CHAR_LOWER_A, CHAR_LOWER_Z);
    c.add_char_range(CHAR_UPPER_A, CHAR_UPPER_Z);
    return c;
}

fn class_of_alnum() -> CharClass {
    var c = class_of_alpha();
    c.add_char_range(CHAR_ZERO, CHAR_NINE);
    return c;
}

fn class_of_upper() -> CharClass {
    var c = CharClass();
    c.add_char_range(CHAR_UPPER_A, CHAR_UPPER_Z);
    return c;
}

fn class_of_lower() -> CharClass {
    var c = CharClass();
    c.add_char_range(CHAR_LOWER_A, CHAR_LOWER_Z);
    return c;
}

fn class_of_xdigit() -> CharClass {
    var c = class_of_digits();
    c.add_char_range(CHAR_LOWER_A, CHAR_LOWER_F);
    c.add_char_range(CHAR_UPPER_A, CHAR_UPPER_F);
    return c;
}

fn class_of_punct() -> CharClass {
    var c = CharClass();
    c.add_range(33, 47);
    c.add_range(58, 64);
    c.add_range(91, 96);
    c.add_range(123, 126);
    return c;
}

fn class_of_print() -> CharClass {
    var c = CharClass();
    c.add_range(32, 126);
    return c;
}

fn class_of_graph() -> CharClass {
    var c = CharClass();
    c.add_range(33, 126);
    return c;
}

fn class_of_cntrl() -> CharClass {
    var c = CharClass();
    c.add_range(0, 31);
    c.add_range(127, 127);
    return c;
}

fn class_of_any(dot_matches_newline: bool) -> CharClass {
    var c = CharClass();
    if (dot_matches_newline) {
        c.add_range(CODE_MIN, CODE_MAX);
        return c;
    }
    c.add_char(CHAR_NEWLINE);
    c.negate();
    return c;
}

// Expand `\d`, `\W` and friends.  Negated shorthands are the positive class
// with the flag flipped, which keeps the two spellings in step.
fn class_of_shorthand(which: Shorthand) -> CharClass {
    if (which == Shorthand.ShortDigit) {
        return class_of_digits();
    }
    if (which == Shorthand.ShortNotDigit) {
        var c = class_of_digits();
        c.negate();
        return c;
    }
    if (which == Shorthand.ShortWord) {
        return class_of_word();
    }
    if (which == Shorthand.ShortNotWord) {
        var c = class_of_word();
        c.negate();
        return c;
    }
    if (which == Shorthand.ShortSpace) {
        return class_of_space();
    }
    if (which == Shorthand.ShortNotSpace) {
        var c = class_of_space();
        c.negate();
        return c;
    }
    if (which == Shorthand.ShortHorizontal) {
        return class_of_horizontal();
    }
    if (which == Shorthand.ShortVertical) {
        return class_of_vertical();
    }
    return CharClass();
}

// `[[:name:]]`.  `found` is set to false when the name is not one of the
// thirteen POSIX classes, and the caller raises ERROR_UNKNOWN_CLASS.
fn class_of_posix_name(name: string, ref found: bool) -> CharClass {
    found = true;
    if (name.compare(POSIX_ALPHA) == 0) {
        return class_of_alpha();
    }
    if (name.compare(POSIX_DIGIT) == 0) {
        return class_of_digits();
    }
    if (name.compare(POSIX_ALNUM) == 0) {
        return class_of_alnum();
    }
    if (name.compare(POSIX_UPPER) == 0) {
        return class_of_upper();
    }
    if (name.compare(POSIX_LOWER) == 0) {
        return class_of_lower();
    }
    if (name.compare(POSIX_SPACE) == 0) {
        return class_of_space();
    }
    if (name.compare(POSIX_BLANK) == 0) {
        return class_of_horizontal();
    }
    if (name.compare(POSIX_PUNCT) == 0) {
        return class_of_punct();
    }
    if (name.compare(POSIX_PRINT) == 0) {
        return class_of_print();
    }
    if (name.compare(POSIX_GRAPH) == 0) {
        return class_of_graph();
    }
    if (name.compare(POSIX_CNTRL) == 0) {
        return class_of_cntrl();
    }
    if (name.compare(POSIX_XDIGIT) == 0) {
        return class_of_xdigit();
    }
    if (name.compare(POSIX_WORD) == 0) {
        return class_of_word();
    }
    found = false;
    return CharClass();
}

// --- Abstract syntax tree ------------------------------------------------
//
// Nodes live in a flat arena on the parser and refer to each other by index.
// A node keeps at most two direct children plus a list, which covers every
// shape the grammar can produce: concatenation and alternation use the list,
// repetition uses `first`, and the leaves use neither.

choice NodeKind {
    NodeEmpty,          // matches the empty string
    NodeLiteral,        // one character
    NodeString,         // a run of characters, produced by the optimiser
    NodeAny,            // .
    NodeClass,          // [...] or a shorthand
    NodeConcat,         // e1 e2 ...
    NodeAlternate,      // e1 | e2 | ...
    NodeRepeat,         // e{lower,upper}
    NodeGroup,          // ( e )
    NodeAnchor,         // ^ $ \u{08} \B \A \z
    NodeBackreference,  // \1 or \k<name>
    NodeInvalid,
}

choice AnchorKind {
    AnchorLineStart,
    AnchorLineEnd,
    AnchorTextStart,
    AnchorTextEnd,
    AnchorWordBoundary,
    AnchorNotWordBoundary,
}

// Whether a quantifier prefers to consume or to stop.
choice Greediness {
    Greedy,
    Lazy,
    Possessive,
}

class Node {

    fn is_leaf(self) -> bool {
        if (self.kind == NodeKind.NodeLiteral) {
            return true;
        }
        if (self.kind == NodeKind.NodeString) {
            return true;
        }
        if (self.kind == NodeKind.NodeAny) {
            return true;
        }
        if (self.kind == NodeKind.NodeClass) {
            return true;
        }
        if (self.kind == NodeKind.NodeAnchor) {
            return true;
        }
        if (self.kind == NodeKind.NodeBackreference) {
            return true;
        }
        if (self.kind == NodeKind.NodeEmpty) {
            return true;
        }
        return false;
    }

    fn has_children(self) -> bool {
        if (not self.children.empty()) {
            return true;
        }
        return self.first != NONE;
    }

    fn child_count(self) -> int {
        if (self.first != NONE) {
            return 1;
        }
        return self.children.size();
    }

    // An anchor consumes nothing, and neither does an empty node or a
    // repetition whose lower bound is zero.
    fn is_zero_width(self) -> bool {
        if (self.kind == NodeKind.NodeAnchor) {
            return true;
        }
        if (self.kind == NodeKind.NodeEmpty) {
            return true;
        }
        return false;
    }

    fn is_unbounded(self) -> bool {
        return self.upper == NONE;
    }

    fn is_optional(self) -> bool {
        return self.lower == 0;
    }

    var kind: NodeKind = NodeKind.NodeInvalid;
    var value: char = '\0';
    var text: string;
    var charclass: CharClass;
    var anchor: AnchorKind = AnchorKind.AnchorLineStart;
    var greed: Greediness = Greediness.Greedy;
    var group_kind: GroupKind = GroupKind.Capturing;
    var group_index: int = -1;
    var reference: int = -1;
    var lower: int = 0;
    var upper: int = -1;
    var first: int = -1;
    var children: vector(int);
    var position: int = 0;
}

// --- Recursive descent pattern parser ------------------------------------
//
// The grammar the parser implements, in the order the functions appear:
//
//     alternation   := concatenation ( '|' concatenation )*
//     concatenation := quantified*
//     quantified    := atom quantifier*
//     quantifier    := ( '*' | '+' | '?' | '{' bound '}' ) '?'?
//     atom          := literal | '.' | class | group | shorthand
//                    | anchor | backreference
//     group         := '(' alternation ')'
//
// `parse_alternation` and `parse_atom` are mutually recursive through
// `parse_group`, which is what gives the parser its depth; `depth` is tracked
// explicitly so a pattern of ten thousand nested groups fails cleanly rather
// than exhausting the native stack.
//
// Bracket expressions are not tokenised in advance.  When the token stream
// offers a ClassOpen the parser drops to character mode over the original
// source text, parses the bracket expression there, and then resynchronises
// the token index to the offset it stopped at.  Bracket expressions have
// their own rules for `-`, `^` and `]`, and running them through the outer
// lexer would mean undoing half of its work.

class GroupName {
    var name: string;
    var index: int = 0;
}

class PatternParser {

    // --- Token stream helpers ---------------------------------------------

    fn token_count(self) -> int {
        return self.tokens.size();
    }

    fn at_end(self) -> bool {
        if (self.index >= self.tokens.size()) {
            return true;
        }
        let ref t: auto = self.tokens[self.index];
        return t.is_end();
    }

    fn current(ref self) -> ref Token {
        return self.tokens[self.index];
    }

    fn current_kind(self) -> TokenKind {
        if (self.index >= self.tokens.size()) {
            return TokenKind.EndOfPattern;
        }
        let ref t: auto = self.tokens[self.index];
        return t.kind;
    }

    fn peek_kind(self, offset: int) -> TokenKind {
        let at = self.index + offset;
        if (at >= self.tokens.size()) {
            return TokenKind.EndOfPattern;
        }
        let ref t: auto = self.tokens[at];
        return t.kind;
    }

    fn current_offset(self) -> int {
        if (self.index >= self.tokens.size()) {
            return self.source.size();
        }
        let ref t: auto = self.tokens[self.index];
        return t.position;
    }

    fn step(ref self) {
        if (self.index < self.tokens.size()) {
            self.index += 1;
        }
    }

    fn accept_kind(ref self, kind: TokenKind) -> bool {
        if (self.current_kind() != kind) {
            return false;
        }
        self.step();
        return true;
    }

    // Skip forward until the token stream is positioned at or after `offset`.
    // Called after the character-mode class parser has consumed a stretch of
    // the source that the lexer had already turned into tokens.
    fn sync_to_offset(ref self, offset: int) {
        while (self.index < self.tokens.size()) {
            let ref t: auto = self.tokens[self.index];
            if (t.position >= offset) {
                break;
            }
            if (t.is_end()) {
                break;
            }
            self.index += 1;
        }
    }

    fn fail(ref self, message: string) -> int {
        if (self.ok) {
            self.ok = false;
            self.error = message;
            self.error_position = self.current_offset();
        }
        return NONE;
    }

    // --- Arena ------------------------------------------------------------

    fn node_count(self) -> int {
        return self.nodes.size();
    }

    fn node_at(ref self, index: int) -> ref Node {
        return self.nodes[index];
    }

    fn new_node(ref self, kind: NodeKind) -> int {
        let index = self.nodes.size();
        var n = Node({.kind = kind});
        n.position = self.current_offset();
        self.nodes.push_back(n);
        return index;
    }

    fn new_literal(ref self, c: char) -> int {
        let index = self.new_node(NodeKind.NodeLiteral);
        let ref n: auto = self.nodes[index];
        n.value = c;
        n.charclass.add_char(c);
        return index;
    }

    fn new_class(ref self, cls: CharClass) -> int {
        let index = self.new_node(NodeKind.NodeClass);
        let ref n: auto = self.nodes[index];
        n.charclass = cls;
        n.charclass.normalise();
        return index;
    }

    fn new_anchor(ref self, which: AnchorKind) -> int {
        let index = self.new_node(NodeKind.NodeAnchor);
        let ref n: auto = self.nodes[index];
        n.anchor = which;
        return index;
    }

    // --- Group name table -------------------------------------------------

    fn find_group_named(self, name: string) -> int {
        for (entry in self.names) {
            if (entry.name.compare(name) == 0) {
                return entry.index;
            }
        }
        return NONE;
    }

    fn register_group_name(ref self, name: string, index: int) {
        var entry = GroupName({.index = index});
        entry.name = name;
        self.names.push_back(entry);
    }

    // --- Entry point ------------------------------------------------------

    fn parse(ref self, pattern: string) -> int {
        self.source = pattern;
        self.nodes.clear();
        self.names.clear();
        self.index = 0;
        self.depth = 0;
        self.group_count = 0;
        self.ok = true;
        self.error = ERROR_NONE;

        var lexer = PatternLexer();
        lexer.source = pattern;
        if (not lexer.tokenize()) {
            self.ok = false;
            self.error = lexer.error;
            self.error_position = lexer.error_position;
            return NONE;
        }
        self.tokens = move(lexer.tokens);

        let root = self.parse_alternation();
        if (not self.ok) {
            return NONE;
        }
        if (not self.at_end()) {
            // The only way to stop early with tokens left is a `)` with no
            // matching `(`, because every other token starts an atom.
            return self.fail(ERROR_UNBALANCED_PAREN);
        }
        self.root = root;
        return root;
    }

    // --- alternation := concatenation ( '|' concatenation )* --------------

    fn parse_alternation(ref self) -> int {
        let first = self.parse_concatenation();
        if (not self.ok) {
            return NONE;
        }
        if (self.current_kind() != TokenKind.AlternationBar) {
            return first;
        }
        let index = self.new_node(NodeKind.NodeAlternate);
        var branches = vector(int)();
        branches.push_back(first);
        while (self.accept_kind(TokenKind.AlternationBar)) {
            let branch = self.parse_concatenation();
            if (not self.ok) {
                return NONE;
            }
            branches.push_back(branch);
        }
        let ref n: auto = self.nodes[index];
        n.children = move(branches);
        return index;
    }

    // --- concatenation := quantified* -------------------------------------
    //
    // Stops at `|`, at `)` and at the end of the token stream.  An empty
    // concatenation is legal - it is how `(|a)` and `a||b` get their empty
    // branches - and yields a NodeEmpty.

    fn parse_concatenation(ref self) -> int {
        var items = vector(int)();
        while (true) {
            if (self.at_end()) {
                break;
            }
            if (self.current_kind() == TokenKind.AlternationBar) {
                break;
            }
            if (self.current_kind() == TokenKind.GroupClose) {
                break;
            }
            let item = self.parse_quantified();
            if (not self.ok) {
                return NONE;
            }
            if (item == NONE) {
                break;
            }
            items.push_back(item);
        }
        if (items.empty()) {
            return self.new_node(NodeKind.NodeEmpty);
        }
        if (items.size() == 1) {
            return items[0];
        }
        let index = self.new_node(NodeKind.NodeConcat);
        let ref n: auto = self.nodes[index];
        n.children = move(items);
        return index;
    }

    // --- quantified := atom quantifier* -----------------------------------
    //
    // Quantifiers are applied left to right, so `a*+` is `(a*)+`.  Each one
    // wraps the node produced so far in a fresh NodeRepeat.

    fn parse_quantified(ref self) -> int {
        var atom = self.parse_atom();
        if (not self.ok) {
            return NONE;
        }
        if (atom == NONE) {
            return NONE;
        }
        while (true) {
            let ref t: auto = self.tokens[self.index];
            if (not t.is_quantifier()) {
                break;
            }
            let ref inner: auto = self.nodes[atom];
            if (inner.kind == NodeKind.NodeAnchor) {
                // `^*` is meaningless and almost always a typo.
                return self.fail(ERROR_NOTHING_TO_REPEAT);
            }
            atom = self.parse_quantifier(atom);
            if (not self.ok) {
                return NONE;
            }
        }
        return atom;
    }

    // Consume one quantifier token and wrap `target` in the repetition it
    // describes.  A trailing `?` makes the repetition lazy; a trailing `+`
    // makes it possessive, which the backtracking matcher honours by
    // discarding the alternatives it would otherwise keep.
    fn parse_quantifier(ref self, target: int) -> int {
        let kind = self.current_kind();
        var lower: int = 0;
        var upper: int = NONE;
        if (kind == TokenKind.Star) {
            lower = 0;
            upper = NONE;
        } else {
            if (kind == TokenKind.Plus) {
                lower = 1;
                upper = NONE;
            } else {
                if (kind == TokenKind.Question) {
                    lower = 0;
                    upper = 1;
                } else {
                    let ref t: auto = self.tokens[self.index];
                    lower = t.lower;
                    upper = t.upper;
                    if (upper != NONE and upper < lower) {
                        return self.fail(ERROR_BAD_REPEAT);
                    }
                    if (lower > MAX_REPEAT) {
                        return self.fail(ERROR_REPEAT_TOO_LARGE);
                    }
                }
            }
        }
        self.step();
        var greed = Greediness.Greedy;
        if (self.current_kind() == TokenKind.Question) {
            greed = Greediness.Lazy;
            self.step();
        } else {
            if (self.current_kind() == TokenKind.Plus) {
                greed = Greediness.Possessive;
                self.step();
            }
        }
        let index = self.new_node(NodeKind.NodeRepeat);
        let ref n: auto = self.nodes[index];
        n.first = target;
        n.lower = lower;
        n.upper = upper;
        n.greed = greed;
        return index;
    }

    // --- atom -------------------------------------------------------------
    //
    // The dispatch point of the parser.  Every branch either produces a node
    // or reports an error; returning NONE without an error set means "this
    // token does not begin an atom", which only `)` can do.

    fn parse_atom(ref self) -> int {
        if (self.at_end()) {
            return NONE;
        }
        let kind = self.current_kind();
        if (kind == TokenKind.Literal) {
            let ref t: auto = self.tokens[self.index];
            let c = t.value;
            self.step();
            return self.new_literal(c);
        }
        if (kind == TokenKind.AnyChar) {
            self.step();
            let index = self.new_node(NodeKind.NodeAny);
            let ref n: auto = self.nodes[index];
            n.charclass = class_of_any(self.dot_matches_newline);
            return index;
        }
        if (kind == TokenKind.ShorthandClass) {
            let ref t: auto = self.tokens[self.index];
            let which = t.shorthand;
            self.step();
            return self.new_class(class_of_shorthand(which));
        }
        if (kind == TokenKind.ClassOpen) {
            return self.parse_class();
        }
        if (kind == TokenKind.GroupOpen) {
            return self.parse_group();
        }
        if (kind == TokenKind.Backreference) {
            return self.parse_backreference();
        }
        if (kind == TokenKind.CaretAnchor) {
            self.step();
            return self.new_anchor(AnchorKind.AnchorLineStart);
        }
        if (kind == TokenKind.DollarAnchor) {
            self.step();
            return self.new_anchor(AnchorKind.AnchorLineEnd);
        }
        if (kind == TokenKind.TextStartAnchor) {
            self.step();
            return self.new_anchor(AnchorKind.AnchorTextStart);
        }
        if (kind == TokenKind.TextEndAnchor) {
            self.step();
            return self.new_anchor(AnchorKind.AnchorTextEnd);
        }
        if (kind == TokenKind.WordBoundary) {
            self.step();
            return self.new_anchor(AnchorKind.AnchorWordBoundary);
        }
        if (kind == TokenKind.NotWordBoundary) {
            self.step();
            return self.new_anchor(AnchorKind.AnchorNotWordBoundary);
        }
        if (kind == TokenKind.Star) {
            return self.fail(ERROR_NOTHING_TO_REPEAT);
        }
        if (kind == TokenKind.Plus) {
            return self.fail(ERROR_NOTHING_TO_REPEAT);
        }
        if (kind == TokenKind.RepeatOpen) {
            return self.fail(ERROR_NOTHING_TO_REPEAT);
        }
        if (kind == TokenKind.Invalid) {
            return self.fail(ERROR_BAD_ESCAPE);
        }
        return NONE;
    }

    // --- group := '(' alternation ')' -------------------------------------
    //
    // The recursive step.  Capturing groups take the next free index before
    // their body is parsed, so `(a(b))` numbers the outer group 1 and the
    // inner group 2 - the order the spelling suggests.

    fn parse_group(ref self) -> int {
        let ref t: auto = self.tokens[self.index];
        let group_kind = t.group_kind;
        var name = t.text;
        self.step();
        self.depth += 1;
        if (self.depth > MAX_GROUP_DEPTH) {
            self.depth -= 1;
            return self.fail(ERROR_DEPTH_EXCEEDED);
        }
        var number = NONE;
        if (group_kind == GroupKind.Capturing) {
            self.group_count += 1;
            number = self.group_count;
        }
        if (group_kind == GroupKind.Named) {
            self.group_count += 1;
            number = self.group_count;
            self.register_group_name(name, number);
        }
        let body = self.parse_alternation();
        if (not self.ok) {
            self.depth -= 1;
            return NONE;
        }
        if (not self.accept_kind(TokenKind.GroupClose)) {
            self.depth -= 1;
            return self.fail(ERROR_UNBALANCED_PAREN);
        }
        self.depth -= 1;
        let index = self.new_node(NodeKind.NodeGroup);
        let ref n: auto = self.nodes[index];
        n.first = body;
        n.group_kind = group_kind;
        n.group_index = number;
        n.text = name;
        return index;
    }

    // --- backreference ----------------------------------------------------

    fn parse_backreference(ref self) -> int {
        let ref t: auto = self.tokens[self.index];
        var target = t.lower;
        if (target == NONE) {
            target = self.find_group_named(t.text);
            if (target == NONE) {
                return self.fail(ERROR_BAD_BACKREF);
            }
        }
        if (target > self.group_count) {
            // Forward references are rejected: a group can only be recalled
            // once it has been opened, and a group that is still open has no
            // captured text yet.
            return self.fail(ERROR_BAD_BACKREF);
        }
        self.step();
        let index = self.new_node(NodeKind.NodeBackreference);
        let ref n: auto = self.nodes[index];
        n.reference = target;
        return index;
    }

    // --- Bracket expressions ----------------------------------------------
    //
    // Character mode.  On entry the token stream sits on the ClassOpen token
    // and `offset` addresses the `[` itself in the source text.  The routine
    // walks the source directly and resynchronises the token index on the way
    // out.
    //
    // The rules a bracket expression obeys:
    //   * a leading `^` negates the whole class
    //   * a `]` in first position - after any `^` - is an ordinary character
    //   * `-` is a range operator except in first or last position
    //   * `[:name:]` names a POSIX class, but a bare `[` is just a bracket
    //   * a shorthand such as `\d` contributes all of its ranges

    fn parse_class(ref self) -> int {
        let open = self.current_offset();
        var at = open + 1;
        var cls = CharClass();
        if (at < self.source.size()) {
            if (self.source.at(at) == CHAR_CARET) {
                cls.negate();
                at += 1;
            }
        }
        var first = true;
        var closed = false;
        while (true) {
            if (at >= self.source.size()) {
                break;
            }
            let c = self.source.at(at);
            if (c == CHAR_RBRACKET and not first) {
                at += 1;
                closed = true;
                break;
            }
            first = false;
            if (c == CHAR_RBRACKET) {
                // Leading `]`: an ordinary member of the class.
                cls.add_char(c);
                at += 1;
                continue;
            }
            if (c == CHAR_LBRACKET and self.looks_like_posix(at)) {
                at = self.parse_posix_class(at, cls);
                if (not self.ok) {
                    return NONE;
                }
                continue;
            }
            if (c == CHAR_BACKSLASH) {
                at = self.parse_class_escape(at, cls);
                if (not self.ok) {
                    return NONE;
                }
                continue;
            }
            // An ordinary character.  It may be the low end of a range, which
            // is decided by what follows it.
            let low = c;
            at += 1;
            if (not self.class_range_follows(at)) {
                cls.add_char(low);
                continue;
            }
            // Step over the hyphen and read the upper bound.
            at += 1;
            let high_char = self.source.at(at);
            if (high_char == CHAR_BACKSLASH) {
                var scratch = CharClass();
                let after = self.parse_class_escape(at, scratch);
                if (not self.ok) {
                    return NONE;
                }
                if (not scratch.is_single_char()) {
                    // `a-\d` has no sensible reading; treat the three parts
                    // as separate members instead of guessing.
                    cls.add_char(low);
                    cls.add_char(CHAR_MINUS);
                    cls.add_class(scratch);
                    at = after;
                    continue;
                }
                let high = scratch.single_char();
                if (char_code(high) < char_code(low)) {
                    return self.fail(ERROR_BAD_RANGE);
                }
                cls.add_char_range(low, high);
                at = after;
                continue;
            }
            if (char_code(high_char) < char_code(low)) {
                return self.fail(ERROR_BAD_RANGE);
            }
            cls.add_char_range(low, high_char);
            at += 1;
        }
        if (not closed) {
            return self.fail(ERROR_UNCLOSED_CLASS);
        }
        self.sync_to_offset(at);
        cls.normalise();
        return self.new_class(cls);
    }

    // True when a `-` at `at` really is a range operator: it must exist, and
    // it must not be the last character before the closing bracket.
    fn class_range_follows(self, at: int) -> bool {
        if (at >= self.source.size()) {
            return false;
        }
        if (self.source.at(at) != CHAR_MINUS) {
            return false;
        }
        let next = at + 1;
        if (next >= self.source.size()) {
            return false;
        }
        if (self.source.at(next) == CHAR_RBRACKET) {
            return false;
        }
        return true;
    }

    // A `[` inside a class opens a POSIX name only when it is followed by a
    // colon and a closing `:]` appears before the class ends.
    fn looks_like_posix(self, at: int) -> bool {
        let colon = at + 1;
        if (colon >= self.source.size()) {
            return false;
        }
        if (self.source.at(colon) != CHAR_COLON) {
            return false;
        }
        var scan = colon + 1;
        if (scan < self.source.size()) {
            if (self.source.at(scan) == CHAR_CARET) {
                scan += 1;
            }
        }
        while (scan < self.source.size()) {
            let c = self.source.at(scan);
            if (c == CHAR_COLON) {
                let close = scan + 1;
                if (close >= self.source.size()) {
                    return false;
                }
                return self.source.at(close) == CHAR_RBRACKET;
            }
            if (not is_alpha(c)) {
                return false;
            }
            scan += 1;
        }
        return false;
    }

    // Consume `[:name:]` and add its ranges to `into`.  `[:^name:]` adds the
    // complement, which is spelled by expanding the class and inverting each
    // gap rather than by flipping the whole class - the surrounding class may
    // already carry a negation of its own.
    fn parse_posix_class(ref self, at: int, ref into: CharClass) -> int {
        var scan = at + 2;
        var complement = false;
        if (scan < self.source.size()) {
            if (self.source.at(scan) == CHAR_CARET) {
                complement = true;
                scan += 1;
            }
        }
        var name = TEXT_EMPTY;
        while (scan < self.source.size()) {
            let c = self.source.at(scan);
            if (c == CHAR_COLON) {
                break;
            }
            name.push_back(c);
            scan += 1;
        }
        // Step over the `:]` pair.
        scan += 2;
        var known = false;
        var expanded = class_of_posix_name(name, known);
        if (not known) {
            Discard(self.fail(ERROR_UNKNOWN_CLASS));
            return scan;
        }
        if (complement) {
            expanded.normalise();
            var inverted = complement_of(expanded);
            into.add_class(inverted);
            return scan;
        }
        into.add_class(expanded);
        return scan;
    }

    // Consume one `\x` sequence inside a bracket expression and add whatever
    // it denotes to `into`.  Shorthands contribute their ranges; everything
    // else contributes a single character.
    fn parse_class_escape(ref self, at: int, ref into: CharClass) -> int {
        let marker_at = at + 1;
        if (marker_at >= self.source.size()) {
            Discard(self.fail(ERROR_TRAILING_BACKSLASH));
            return self.source.size();
        }
        let marker = self.source.at(marker_at);
        if (marker == CHAR_LOWER_D) {
            into.add_class(class_of_digits());
            return marker_at + 1;
        }
        if (marker == CHAR_UPPER_D) {
            into.add_class(complement_of(class_of_digits()));
            return marker_at + 1;
        }
        if (marker == CHAR_LOWER_W) {
            into.add_class(class_of_word());
            return marker_at + 1;
        }
        if (marker == CHAR_UPPER_W) {
            into.add_class(complement_of(class_of_word()));
            return marker_at + 1;
        }
        if (marker == CHAR_LOWER_S) {
            into.add_class(class_of_space());
            return marker_at + 1;
        }
        if (marker == CHAR_UPPER_S) {
            into.add_class(complement_of(class_of_space()));
            return marker_at + 1;
        }
        if (marker == CHAR_LOWER_H) {
            into.add_class(class_of_horizontal());
            return marker_at + 1;
        }
        if (marker == CHAR_LOWER_V) {
            into.add_class(class_of_vertical());
            return marker_at + 1;
        }
        if (marker == CHAR_LOWER_X) {
            let high_at = marker_at + 1;
            let low_at = marker_at + 2;
            if (low_at >= self.source.size()) {
                Discard(self.fail(ERROR_BAD_ESCAPE));
                return self.source.size();
            }
            let high = self.source.at(high_at);
            let low = self.source.at(low_at);
            if (not is_hex_digit(high) or not is_hex_digit(low)) {
                Discard(self.fail(ERROR_BAD_ESCAPE));
                return low_at + 1;
            }
            into.add_range(hex_value(high) * 16 + hex_value(low), hex_value(high) * 16 + hex_value(low));
            return low_at + 1;
        }
        if (marker == CHAR_LOWER_B) {
            // `\u{08}` is a backspace inside a class, not a word boundary.
            into.add_char(CHAR_BACKSPACE);
            return marker_at + 1;
        }
        into.add_char(decode_escape(marker));
        return marker_at + 1;
    }

    var source: string;
    var tokens: vector(Token);
    var nodes: vector(Node);
    var names: vector(GroupName);
    var index: int = 0;
    var depth: int = 0;
    var group_count: int = 0;
    var root: int = -1;
    var error: string;
    var error_position: int = 0;
    var ok: bool = true;
    var dot_matches_newline: bool = false;
    var ignore_case: bool = false;
}

// Build the set complement of a normalised class as an explicit range list.
// Flipping the `negated` flag would be cheaper, but a complement built this
// way can be unioned with other classes, which the bracket parser needs.
fn complement_of(cls: CharClass) -> CharClass {
    var source = cls;
    source.normalise();
    var out = CharClass();
    var cursor = CODE_MIN;
    for (r in source.ranges) {
        if (r.low > cursor) {
            out.add_range(cursor, r.low - 1);
        }
        if (r.high + 1 > cursor) {
            cursor = r.high + 1;
        }
    }
    if (cursor <= CODE_MAX) {
        out.add_range(cursor, CODE_MAX);
    }
    out.normalise();
    return out;
}

// --- Thompson NFA construction -------------------------------------------
//
// Every AST node is turned into a fragment: a start state plus a list of
// dangling exits that the caller patches to whatever comes next.  The
// construction is the classical one, so the resulting automaton has at most
// two transitions out of any state and no more states than the pattern has
// characters.
//
// Backreferences have no NFA form - they are not regular - so the builder
// emits a Backref state that only the backtracking matcher understands.  A
// program containing one is marked `has_backreference` and the simulator
// refuses to run it.

choice StateKind {
    StateChar,          // consume one character from a class
    StateSplit,         // epsilon to both `out` and `alternate`
    StateJump,          // epsilon to `out`
    StateSave,          // record the current offset in capture slot `slot`
    StateAssert,        // zero width assertion
    StateBackref,       // re-match the text captured by a group
    StateMatch,     // accept
}

class NfaState {

    fn consumes(self) -> bool {
        return self.kind == StateKind.StateChar;
    }

    fn is_epsilon(self) -> bool {
        if (self.kind == StateKind.StateSplit) {
            return true;
        }
        if (self.kind == StateKind.StateJump) {
            return true;
        }
        if (self.kind == StateKind.StateSave) {
            return true;
        }
        if (self.kind == StateKind.StateAssert) {
            return true;
        }
        return false;
    }

    fn is_accepting(self) -> bool {
        return self.kind == StateKind.StateMatch;
    }

    fn accepts(self, c: char, folded: bool) -> bool {
        if (self.kind != StateKind.StateChar) {
            return false;
        }
        if (folded) {
            return self.charclass.matches_folded(c);
        }
        return self.charclass.matches(c);
    }

    var kind: StateKind = StateKind.StateJump;
    var charclass: CharClass;
    var anchor: AnchorKind = AnchorKind.AnchorLineStart;
    var slot: int = 0;
    var reference: int = 0;
    var out: int = -1;
    var alternate: int = -1;
}

// A dangling exit: state `state`, and which of its two out edges is unset.
class Patch {
    var state: int = 0;
    var second: bool = false;
}

// A partially built automaton.
class Fragment {

    fn valid(self) -> bool {
        return self.start != NONE;
    }

    fn exit_count(self) -> int {
        return self.exits.size();
    }

    fn absorb(ref self, other: Fragment) {
        for (p in other.exits) {
            self.exits.push_back(p);
        }
    }

    var start: int = -1;
    var exits: vector(Patch);
}

class NfaBuilder {

    fn state_count(self) -> int {
        return self.states.size();
    }

    fn new_state(ref self, kind: StateKind) -> int {
        let index = self.states.size();
        self.states.push_back(NfaState({.kind = kind}));
        return index;
    }

    // Point every exit of `frag` at `target`.
    fn patch(ref self, frag: Fragment, target: int) {
        for (p in frag.exits) {
            let ref s: auto = self.states[p.state];
            if (p.second) {
                s.alternate = target;
            } else {
                s.out = target;
            }
        }
    }

    fn single_exit(self, state: int, second: bool) -> Fragment {
        var frag = Fragment({.start = state});
        frag.exits.push_back(Patch({.state = state, .second = second}));
        return frag;
    }

    // --- Entry point ------------------------------------------------------

    fn build(ref self, ref parser: PatternParser, root: int) -> bool {
        self.states.clear();
        self.has_backreference = false;
        self.ok = true;
        let frag = self.build_node(parser, root);
        if (not self.ok) {
            return false;
        }
        let accept = self.new_state(StateKind.StateMatch);
        self.patch(frag, accept);
        self.start = frag.start;
        self.accept = accept;
        return true;
    }

    // --- The recursive core -----------------------------------------------

    fn build_node(ref self, ref parser: PatternParser, index: int) -> Fragment {
        if (index == NONE) {
            return self.build_empty();
        }
        let ref n: auto = parser.nodes[index];
        if (n.kind == NodeKind.NodeEmpty) {
            return self.build_empty();
        }
        if (n.kind == NodeKind.NodeLiteral) {
            return self.build_class(n.charclass);
        }
        if (n.kind == NodeKind.NodeAny) {
            return self.build_class(n.charclass);
        }
        if (n.kind == NodeKind.NodeClass) {
            return self.build_class(n.charclass);
        }
        if (n.kind == NodeKind.NodeString) {
            return self.build_string(n.text);
        }
        if (n.kind == NodeKind.NodeAnchor) {
            return self.build_anchor(n.anchor);
        }
        if (n.kind == NodeKind.NodeBackreference) {
            return self.build_backreference(n.reference);
        }
        if (n.kind == NodeKind.NodeConcat) {
            return self.build_concat(parser, index);
        }
        if (n.kind == NodeKind.NodeAlternate) {
            return self.build_alternate(parser, index);
        }
        if (n.kind == NodeKind.NodeRepeat) {
            return self.build_repeat(parser, index);
        }
        if (n.kind == NodeKind.NodeGroup) {
            return self.build_group(parser, index);
        }
        self.ok = false;
        return Fragment();
    }

    fn build_empty(ref self) -> Fragment {
        let s = self.new_state(StateKind.StateJump);
        return self.single_exit(s, false);
    }

    fn build_class(ref self, cls: CharClass) -> Fragment {
        let s = self.new_state(StateKind.StateChar);
        let ref state: auto = self.states[s];
        state.charclass = cls;
        state.charclass.normalise();
        return self.single_exit(s, false);
    }

    // A literal run compiles to a chain of single character states.  The
    // simulator has no special case for it, but the compiled program does.
    fn build_string(ref self, text: string) -> Fragment {
        if (text.empty()) {
            return self.build_empty();
        }
        var head = NONE;
        var previous = NONE;
        var i: int = 0;
        while (i < text.size()) {
            let s = self.new_state(StateKind.StateChar);
            let ref state: auto = self.states[s];
            state.charclass.add_char(text.at(i));
            if (head == NONE) {
                head = s;
            } else {
                let ref prior: auto = self.states[previous];
                prior.out = s;
            }
            previous = s;
            i += 1;
        }
        var frag = Fragment({.start = head});
        frag.exits.push_back(Patch({.state = previous, .second = false}));
        return frag;
    }

    fn build_anchor(ref self, which: AnchorKind) -> Fragment {
        let s = self.new_state(StateKind.StateAssert);
        let ref state: auto = self.states[s];
        state.anchor = which;
        return self.single_exit(s, false);
    }

    fn build_backreference(ref self, group: int) -> Fragment {
        self.has_backreference = true;
        let s = self.new_state(StateKind.StateBackref);
        let ref state: auto = self.states[s];
        state.reference = group;
        return self.single_exit(s, false);
    }

    // e1 e2 ... : chain the fragments, patching each one's exits to the next
    // fragment's start.  The exits of the last fragment become the exits of
    // the whole.
    fn build_concat(ref self, ref parser: PatternParser, index: int) -> Fragment {
        let ref n: auto = parser.nodes[index];
        var result = Fragment();
        var started = false;
        for (child in n.children) {
            let piece = self.build_node(parser, child);
            if (not self.ok) {
                return Fragment();
            }
            if (not started) {
                result = piece;
                started = true;
            } else {
                self.patch(result, piece.start);
                result.exits.clear();
                result.absorb(piece);
            }
        }
        if (not started) {
            return self.build_empty();
        }
        return result;
    }

    // e1 | e2 | ... : a chain of Split states, each preferring its left
    // branch.  The last branch takes the final split's alternate edge, so no
    // split is ever left with a dangling second edge.
    fn build_alternate(ref self, ref parser: PatternParser, index: int) -> Fragment {
        let ref n: auto = parser.nodes[index];
        var result = Fragment();
        var previous_split = NONE;
        var count: int = 0;
        let total = n.children.size();
        for (child in n.children) {
            let piece = self.build_node(parser, child);
            if (not self.ok) {
                return Fragment();
            }
            count += 1;
            if (count == total) {
                if (previous_split == NONE) {
                    return piece;
                }
                let ref split: auto = self.states[previous_split];
                split.alternate = piece.start;
                result.absorb(piece);
                return result;
            }
            let split = self.new_state(StateKind.StateSplit);
            let ref s: auto = self.states[split];
            s.out = piece.start;
            if (previous_split == NONE) {
                result.start = split;
            } else {
                let ref prior: auto = self.states[previous_split];
                prior.alternate = split;
            }
            result.absorb(piece);
            previous_split = split;
        }
        return result;
    }

    fn build_group(ref self, ref parser: PatternParser, index: int) -> Fragment {
        let ref n: auto = parser.nodes[index];
        let group = n.group_index;
        if (group == NONE) {
            return self.build_node(parser, n.first);
        }
        let open = self.new_state(StateKind.StateSave);
        let ref open_state: auto = self.states[open];
        open_state.slot = group * 2;
        let body = self.build_node(parser, n.first);
        if (not self.ok) {
            return Fragment();
        }
        open_state.out = body.start;
        let close = self.new_state(StateKind.StateSave);
        let ref close_state: auto = self.states[close];
        close_state.slot = group * 2 + 1;
        self.patch(body, close);
        return self.single_exit_from(open, close);
    }

    // Helper for fragments whose start and exit are different states.
    fn single_exit_from(self, start: int, exit: int) -> Fragment {
        var frag = Fragment({.start = start});
        frag.exits.push_back(Patch({.state = exit, .second = false}));
        return frag;
    }

    // --- Repetition -------------------------------------------------------
    //
    // `e{lower,upper}` is expanded into copies of `e`: `lower` mandatory ones
    // followed by either an unbounded star, when `upper` is NONE, or by
    // `upper - lower` optional ones.  Expansion keeps the automaton a plain
    // Thompson NFA with no counters, at the cost of size - which is why the
    // parser caps the bound at MAX_REPEAT.

    fn build_repeat(ref self, ref parser: PatternParser, index: int) -> Fragment {
        let ref n: auto = parser.nodes[index];
        let lower = n.lower;
        let upper = n.upper;
        let body = n.first;
        let lazy = n.greed == Greediness.Lazy;
        if (lower == 0 and upper == NONE) {
            return self.build_star(parser, body, lazy);
        }
        if (lower == 1 and upper == NONE) {
            return self.build_plus(parser, body, lazy);
        }
        if (lower == 0 and upper == 1) {
            return self.build_optional(parser, body, lazy);
        }
        var result = Fragment();
        var started = false;
        var i: int = 0;
        while (i < lower) {
            let piece = self.build_node(parser, body);
            if (not self.ok) {
                return Fragment();
            }
            if (not started) {
                result = piece;
                started = true;
            } else {
                self.patch(result, piece.start);
                result.exits.clear();
                result.absorb(piece);
            }
            i += 1;
        }
        if (upper == NONE) {
            let tail = self.build_star(parser, body, lazy);
            if (not started) {
                return tail;
            }
            self.patch(result, tail.start);
            result.exits.clear();
            result.absorb(tail);
            return result;
        }
        var extra = upper - lower;
        while (extra > 0) {
            let piece = self.build_optional(parser, body, lazy);
            if (not self.ok) {
                return Fragment();
            }
            if (not started) {
                result = piece;
                started = true;
            } else {
                self.patch(result, piece.start);
                result.exits.clear();
                result.absorb(piece);
            }
            extra -= 1;
        }
        if (not started) {
            return self.build_empty();
        }
        return result;
    }

    // e* : a split that either enters the body - which loops back - or skips
    // it.  A lazy star simply swaps the two edges so the skip is preferred.
    fn build_star(ref self, ref parser: PatternParser, body: int, lazy: bool) -> Fragment {
        let split = self.new_state(StateKind.StateSplit);
        let piece = self.build_node(parser, body);
        if (not self.ok) {
            return Fragment();
        }
        self.patch(piece, split);
        let ref s: auto = self.states[split];
        if (lazy) {
            s.alternate = piece.start;
            return self.single_exit(split, false);
        }
        s.out = piece.start;
        return self.single_exit(split, true);
    }

    // e+ : the body first, then a split that loops back or falls through.
    fn build_plus(ref self, ref parser: PatternParser, body: int, lazy: bool) -> Fragment {
        let piece = self.build_node(parser, body);
        if (not self.ok) {
            return Fragment();
        }
        let split = self.new_state(StateKind.StateSplit);
        self.patch(piece, split);
        let ref s: auto = self.states[split];
        if (lazy) {
            s.alternate = piece.start;
            return self.single_exit_from(piece.start, split);
        }
        s.out = piece.start;
        var frag = Fragment({.start = piece.start});
        frag.exits.push_back(Patch({.state = split, .second = true}));
        return frag;
    }

    // e? : one split, no loop.
    fn build_optional(ref self, ref parser: PatternParser, body: int, lazy: bool) -> Fragment {
        let split = self.new_state(StateKind.StateSplit);
        let piece = self.build_node(parser, body);
        if (not self.ok) {
            return Fragment();
        }
        let ref s: auto = self.states[split];
        var frag = Fragment({.start = split});
        if (lazy) {
            s.alternate = piece.start;
            frag.exits.push_back(Patch({.state = split, .second = false}));
        } else {
            s.out = piece.start;
            frag.exits.push_back(Patch({.state = split, .second = true}));
        }
        frag.absorb(piece);
        return frag;
    }

    var states: vector(NfaState);
    var start: int = -1;
    var accept: int = -1;
    var has_backreference: bool = false;
    var ok: bool = true;
}

// --- NFA simulation ------------------------------------------------------
//
// The simulator runs every alternative at once: it keeps a set of NFA states
// and advances the whole set one input character at a time.  That gives a
// running time of O(text * states) with no backtracking at all, which is the
// point of the construction - but it cannot report capture groups, and it
// cannot handle backreferences, so the backtracking matcher below still earns
// its keep.
//
// The state set is a sparse set: `dense` lists the members in insertion order
// and `stamp` records, per state, the generation in which it was added.
// Clearing the set is then a single increment.

class StateSet {

    fn size(self) -> int {
        return self.dense.size();
    }

    fn empty(self) -> bool {
        return self.dense.empty();
    }

    fn prepare(ref self, capacity: int) {
        self.stamp.resize(capacity);
        self.generation = 1;
        var i: int = 0;
        while (i < capacity) {
            self.stamp[i] = 0;
            i += 1;
        }
        self.dense.clear();
    }

    fn clear(ref self) {
        self.generation += 1;
        self.dense.clear();
    }

    fn contains(self, state: int) -> bool {
        if (state < 0) {
            return false;
        }
        if (state >= self.stamp.size()) {
            return false;
        }
        return self.stamp[state] == self.generation;
    }

    // Returns false when the state was already present, which is what stops
    // the epsilon closure below from looping forever.
    fn insert(ref self, state: int) -> bool {
        if (self.contains(state)) {
            return false;
        }
        self.stamp[state] = self.generation;
        self.dense.push_back(state);
        return true;
    }

    fn at(self, i: int) -> int {
        return self.dense[i];
    }

    var dense: vector(int);
    var stamp: vector(int);
    var generation: int = 1;
}

// The zero width assertions, evaluated against a position in the subject.
fn assertion_holds(which: AnchorKind, text: string, pos: int, multiline: bool) -> bool {
    if (which == AnchorKind.AnchorTextStart) {
        return pos == 0;
    }
    if (which == AnchorKind.AnchorTextEnd) {
        return pos == text.size();
    }
    if (which == AnchorKind.AnchorLineStart) {
        if (pos == 0) {
            return true;
        }
        if (not multiline) {
            return false;
        }
        return text.at(pos - 1) == CHAR_NEWLINE;
    }
    if (which == AnchorKind.AnchorLineEnd) {
        if (pos == text.size()) {
            return true;
        }
        if (not multiline) {
            return false;
        }
        return text.at(pos) == CHAR_NEWLINE;
    }
    let before = word_char_before(text, pos);
    let after = word_char_at(text, pos);
    if (which == AnchorKind.AnchorWordBoundary) {
        return before != after;
    }
    return before == after;
}

fn word_char_before(text: string, pos: int) -> bool {
    if (pos <= 0) {
        return false;
    }
    return is_word_char(text.at(pos - 1));
}

fn word_char_at(text: string, pos: int) -> bool {
    if (pos >= text.size()) {
        return false;
    }
    return is_word_char(text.at(pos));
}

class NfaSimulator {

    fn ready(self) -> bool {
        if (self.states.empty()) {
            return false;
        }
        return self.start != NONE;
    }

    fn load(ref self, builder: NfaBuilder) {
        self.states = builder.states;
        self.start = builder.start;
        self.accept = builder.accept;
        self.current.prepare(self.states.size());
        self.next.prepare(self.states.size());
    }

    // Add `state` and everything reachable from it without consuming input.
    // Splits fan out, jumps and saves fall through, and assertions are
    // evaluated on the spot against `pos`.
    fn add_state(ref self, ref set: StateSet, state: int, pos: int) {
        if (state == NONE) {
            return;
        }
        if (not set.insert(state)) {
            return;
        }
        let ref s: auto = self.states[state];
        if (s.kind == StateKind.StateSplit) {
            self.add_state(set, s.out, pos);
            self.add_state(set, s.alternate, pos);
            return;
        }
        if (s.kind == StateKind.StateJump) {
            self.add_state(set, s.out, pos);
            return;
        }
        if (s.kind == StateKind.StateSave) {
            // The simulator does not record captures, so a save is a jump.
            self.add_state(set, s.out, pos);
            return;
        }
        if (s.kind == StateKind.StateAssert) {
            if (assertion_holds(s.anchor, self.text, pos, self.multiline)) {
                self.add_state(set, s.out, pos);
            }
            return;
        }
    }

    fn set_contains_accept(self, set: StateSet) -> bool {
        var i: int = 0;
        while (i < set.size()) {
            let index = set.at(i);
            let ref s: auto = self.states[index];
            if (s.is_accepting()) {
                return true;
            }
            i += 1;
        }
        return false;
    }

    // Advance the whole set across one character.
    fn step(ref self, c: char, pos: int) {
        self.next.clear();
        var i: int = 0;
        while (i < self.current.size()) {
            let index = self.current.at(i);
            let ref s: auto = self.states[index];
            if (s.consumes()) {
                if (s.accepts(c, self.ignore_case)) {
                    self.add_state(self.next, s.out, pos + 1);
                }
            }
            i += 1;
        }
        swap(self.current, self.next);
    }

    // Run from `from` and report the offset just past the longest match that
    // begins there, or NONE when nothing matches.
    fn run_from(ref self, from: int) -> int {
        self.current.clear();
        self.add_state(self.current, self.start, from);
        var best = NONE;
        if (self.set_contains_accept(self.current)) {
            best = from;
        }
        var pos = from;
        while (pos < self.text.size()) {
            if (self.current.empty()) {
                break;
            }
            self.step(self.text.at(pos), pos);
            pos += 1;
            if (self.set_contains_accept(self.current)) {
                best = pos;
            }
            self.steps += 1;
        }
        return best;
    }

    // Leftmost match: try every starting offset in turn.  A pattern anchored
    // at the text start could skip the loop entirely, and the optimiser marks
    // such programs so the API can do exactly that.
    fn search_from(ref self, from: int) -> bool {
        var start = from;
        while (start <= self.text.size()) {
            let end = self.run_from(start);
            if (end != NONE) {
                self.match_begin = start;
                self.match_end = end;
                return true;
            }
            start += 1;
        }
        self.match_begin = NONE;
        self.match_end = NONE;
        return false;
    }

    fn matches_whole(ref self) -> bool {
        let end = self.run_from(0);
        if (end == NONE) {
            return false;
        }
        return end == self.text.size();
    }

    var states: vector(NfaState);
    var current: StateSet;
    var next: StateSet;
    var text: string;
    var start: int = -1;
    var accept: int = -1;
    var match_begin: int = -1;
    var match_end: int = -1;
    var steps: int = 0;
    var ignore_case: bool = false;
    var multiline: bool = false;
}

// --- Backtracking matcher ------------------------------------------------
//
// The simulator above is the fast path, but it reports only where a match
// ended.  This matcher walks the AST directly, keeps a capture table, honours
// greedy, lazy and possessive quantifiers, and can re-match captured text for
// a backreference.  In exchange it can backtrack exponentially, so every step
// is counted against a budget.
//
// Continuations are explicit.  Rather than passing a closure - which the
// language has no form for - the matcher stores frames in a list and passes
// the index of the head.  A frame says what is still to be matched after the
// current node succeeds, so `run_continuation` is the "what comes next" half
// of the recursion and `match_node` is the "what is here" half.  The two call
// each other, which is exactly the shape a continuation passing matcher has
// in any language.

choice MatchStatus {
    Matched,
    NoMatch,
    Exhausted,
}

choice ContKind {
    ContNode,           // match `node`, then continue with `next`
    ContGroupEnd,       // close capture group `slot`, then continue
    ContRepeat,         // resume a repetition at iteration `count`
    ContDone,       // the whole pattern has been matched
}

class Capture {

    fn valid(self) -> bool {
        if (self.begin < 0) {
            return false;
        }
        return self.end >= self.begin;
    }

    fn length(self) -> int {
        if (not self.valid()) {
            return 0;
        }
        return self.end - self.begin;
    }

    fn text_of(self, subject: string) -> string {
        if (not self.valid()) {
            return TEXT_EMPTY;
        }
        return subject.substr(self.begin, self.length());
    }

    var begin: int = -1;
    var end: int = -1;
}

class ContFrame {
    var kind: ContKind = ContKind.ContDone;
    var node: int = -1;
    var next: int = -1;
    var slot: int = -1;
    var mark: int = 0;
    var count: int = 0;
}

class BacktrackMatcher {

    // --- Set up -----------------------------------------------------------

    fn load(ref self, parser: PatternParser) {
        self.nodes = parser.nodes;
        self.root = parser.root;
        self.group_count = parser.group_count;
        self.names = parser.names;
    }

    fn reset(ref self) {
        self.frames.clear();
        self.steps = 0;
        self.status = MatchStatus.NoMatch;
        self.captures.clear();
        var i: int = 0;
        while (i <= self.group_count) {
            self.captures.push_back(Capture());
            i += 1;
        }
    }

    fn capture_count(self) -> int {
        return self.captures.size();
    }

    fn capture_at(ref self, index: int) -> ref Capture {
        return self.captures[index];
    }

    fn group_named(self, name: string) -> int {
        for (entry in self.names) {
            if (entry.name.compare(name) == 0) {
                return entry.index;
            }
        }
        return NONE;
    }

    // Charge every recursive step so a runaway pattern stops rather than
    // hanging.  `spend` returns false once the budget is gone, and every
    // caller treats that as an immediate failure.
    fn spend(ref self) -> bool {
        self.steps += 1;
        if (self.steps > self.budget) {
            self.status = MatchStatus.Exhausted;
            return false;
        }
        return true;
    }

    fn exhausted(self) -> bool {
        return self.status == MatchStatus.Exhausted;
    }

    // --- Frame allocation -------------------------------------------------

    fn push_frame(ref self, frame: ContFrame) -> int {
        let index = self.frames.size();
        self.frames.push_back(frame);
        return index;
    }

    fn push_node(ref self, node: int, next: int) -> int {
        var f = ContFrame({.kind = ContKind.ContNode, .node = node, .next = next});
        return self.push_frame(f);
    }

    fn push_group_end(ref self, slot: int, mark: int, next: int) -> int {
        var f = ContFrame({.kind = ContKind.ContGroupEnd, .next = next});
        f.slot = slot;
        f.mark = mark;
        return self.push_frame(f);
    }

    fn push_repeat(ref self, node: int, count: int, mark: int, next: int) -> int {
        var f = ContFrame({.kind = ContKind.ContRepeat, .node = node, .next = next});
        f.count = count;
        f.mark = mark;
        return self.push_frame(f);
    }

    // --- The continuation half --------------------------------------------
    //
    // Returns the offset the whole match reached, or NONE on failure.

    fn run_continuation(ref self, cont: int, pos: int) -> int {
        if (not self.spend()) {
            return NONE;
        }
        if (cont == NONE) {
            // Nothing left to match: the pattern has succeeded here.
            return pos;
        }
        let kind = self.frames[cont].kind;
        if (kind == ContKind.ContDone) {
            return pos;
        }
        if (kind == ContKind.ContNode) {
            let node = self.frames[cont].node;
            let next = self.frames[cont].next;
            return self.match_node(node, pos, next);
        }
        if (kind == ContKind.ContGroupEnd) {
            let slot = self.frames[cont].slot;
            let mark = self.frames[cont].mark;
            let next = self.frames[cont].next;
            var saved_begin = NONE;
            var saved_end = NONE;
            if (slot < self.captures.size()) {
                let ref existing: auto = self.captures[slot];
                saved_begin = existing.begin;
                saved_end = existing.end;
                let ref cap: auto = self.captures[slot];
                cap.begin = mark;
                cap.end = pos;
            }
            let result = self.run_continuation(next, pos);
            if (result == NONE) {
                // Undo the capture: a group that took part in a failed
                // attempt must not leak its text into the final table.
                if (slot < self.captures.size()) {
                    let ref cap: auto = self.captures[slot];
                    cap.begin = saved_begin;
                    cap.end = saved_end;
                }
            }
            return result;
        }
        let node = self.frames[cont].node;
        let count = self.frames[cont].count;
        let mark = self.frames[cont].mark;
        let next = self.frames[cont].next;
        if (pos == mark) {
            // The body matched the empty string.  Repeating it again would
            // never terminate, so the repetition ends here.
            let ref n: auto = self.nodes[node];
            if (count >= n.lower) {
                return self.run_continuation(next, pos);
            }
            return NONE;
        }
        return self.match_repeat(node, pos, count, next);
    }

    // --- The node half ----------------------------------------------------

    fn match_node(ref self, node: int, pos: int, cont: int) -> int {
        if (not self.spend()) {
            return NONE;
        }
        if (node == NONE) {
            return self.run_continuation(cont, pos);
        }
        let kind = self.nodes[node].kind;
        if (kind == NodeKind.NodeEmpty) {
            return self.run_continuation(cont, pos);
        }
        if (kind == NodeKind.NodeLiteral) {
            return self.match_literal(node, pos, cont);
        }
        if (kind == NodeKind.NodeString) {
            return self.match_string(node, pos, cont);
        }
        if (kind == NodeKind.NodeAny) {
            return self.match_class(node, pos, cont);
        }
        if (kind == NodeKind.NodeClass) {
            return self.match_class(node, pos, cont);
        }
        if (kind == NodeKind.NodeAnchor) {
            return self.match_anchor(node, pos, cont);
        }
        if (kind == NodeKind.NodeBackreference) {
            return self.match_backreference(node, pos, cont);
        }
        if (kind == NodeKind.NodeConcat) {
            return self.match_concat(node, pos, cont);
        }
        if (kind == NodeKind.NodeAlternate) {
            return self.match_alternate(node, pos, cont);
        }
        if (kind == NodeKind.NodeGroup) {
            return self.match_group(node, pos, cont);
        }
        if (kind == NodeKind.NodeRepeat) {
            return self.match_repeat(node, pos, 0, cont);
        }
        return NONE;
    }

    fn match_literal(ref self, node: int, pos: int, cont: int) -> int {
        if (pos >= self.text.size()) {
            return NONE;
        }
        let expected = self.nodes[node].value;
        let actual = self.text.at(pos);
        if (self.ignore_case) {
            if (not same_char_folded(expected, actual)) {
                return NONE;
            }
        } else {
            if (expected != actual) {
                return NONE;
            }
        }
        return self.run_continuation(cont, pos + 1);
    }

    // A literal run, produced by the optimiser when it fuses adjacent
    // literals.  Matching it in one go saves a continuation hop per
    // character, which is the whole reason the optimisation exists.
    fn match_string(ref self, node: int, pos: int, cont: int) -> int {
        let ref text: auto = self.nodes[node].text;
        if (pos + text.size() > self.text.size()) {
            return NONE;
        }
        var i: int = 0;
        while (i < text.size()) {
            let expected = text.at(i);
            let actual = self.text.at(pos + i);
            if (self.ignore_case) {
                if (not same_char_folded(expected, actual)) {
                    return NONE;
                }
            } else {
                if (expected != actual) {
                    return NONE;
                }
            }
            i += 1;
        }
        return self.run_continuation(cont, pos + text.size());
    }

    fn match_class(ref self, node: int, pos: int, cont: int) -> int {
        if (pos >= self.text.size()) {
            return NONE;
        }
        let c = self.text.at(pos);
        let ref cls: auto = self.nodes[node].charclass;
        var hit = false;
        if (self.ignore_case) {
            hit = cls.matches_folded(c);
        } else {
            hit = cls.matches(c);
        }
        if (not hit) {
            return NONE;
        }
        return self.run_continuation(cont, pos + 1);
    }

    fn match_anchor(ref self, node: int, pos: int, cont: int) -> int {
        let which = self.nodes[node].anchor;
        if (not assertion_holds(which, self.text, pos, self.multiline)) {
            return NONE;
        }
        return self.run_continuation(cont, pos);
    }

    // Re-match the exact text a group captured earlier.  An unset group
    // matches the empty string, which is what most engines do.
    fn match_backreference(ref self, node: int, pos: int, cont: int) -> int {
        let group = self.nodes[node].reference;
        if (group >= self.captures.size()) {
            return NONE;
        }
        let ref cap: auto = self.captures[group];
        if (not cap.valid()) {
            return self.run_continuation(cont, pos);
        }
        let length = cap.length();
        if (pos + length > self.text.size()) {
            return NONE;
        }
        var i: int = 0;
        while (i < length) {
            let expected = self.text.at(cap.begin + i);
            let actual = self.text.at(pos + i);
            if (self.ignore_case) {
                if (not same_char_folded(expected, actual)) {
                    return NONE;
                }
            } else {
                if (expected != actual) {
                    return NONE;
                }
            }
            i += 1;
        }
        return self.run_continuation(cont, pos + length);
    }

    // Chain the children onto the continuation from the right, so the first
    // child is matched first and every later child is waiting in a frame.
    fn match_concat(ref self, node: int, pos: int, cont: int) -> int {
        let count = self.nodes[node].children.size();
        if (count == 0) {
            return self.run_continuation(cont, pos);
        }
        var chain = cont;
        var i = count - 1;
        while (i >= 0) {
            let child = self.nodes[node].children[i];
            chain = self.push_node(child, chain);
            i -= 1;
        }
        return self.run_continuation(chain, pos);
    }

    // Try each branch in order with the same continuation.  Ordering is what
    // makes alternation leftmost-biased, so `a|ab` matches just `a`.
    fn match_alternate(ref self, node: int, pos: int, cont: int) -> int {
        let count = self.nodes[node].children.size();
        var i: int = 0;
        while (i < count) {
            let child = self.nodes[node].children[i];
            let result = self.match_node(child, pos, cont);
            if (result != NONE) {
                return result;
            }
            if (self.exhausted()) {
                return NONE;
            }
            i += 1;
        }
        return NONE;
    }

    fn match_group(ref self, node: int, pos: int, cont: int) -> int {
        let body = self.nodes[node].first;
        let group = self.nodes[node].group_index;
        if (group == NONE) {
            return self.match_node(body, pos, cont);
        }
        let closing = self.push_group_end(group, pos, cont);
        return self.match_node(body, pos, closing);
    }

    // --- Repetition -------------------------------------------------------
    //
    // `count` is how many iterations have already succeeded.  A greedy
    // repetition tries one more iteration before it tries the continuation; a
    // lazy one tries the continuation first.  A possessive repetition takes
    // as many iterations as it can and never gives one back.

    fn match_repeat(ref self, node: int, pos: int, count: int, cont: int) -> int {
        if (not self.spend()) {
            return NONE;
        }
        let lower = self.nodes[node].lower;
        let upper = self.nodes[node].upper;
        let body = self.nodes[node].first;
        let greed = self.nodes[node].greed;
        var may_continue = count >= lower;
        var may_repeat = true;
        if (upper != NONE) {
            if (count >= upper) {
                may_repeat = false;
            }
        }
        if (greed == Greediness.Possessive) {
            return self.match_possessive(node, pos, count, cont);
        }
        if (greed == Greediness.Lazy) {
            if (may_continue) {
                let stop = self.run_continuation(cont, pos);
                if (stop != NONE) {
                    return stop;
                }
                if (self.exhausted()) {
                    return NONE;
                }
            }
            if (not may_repeat) {
                return NONE;
            }
            let resume = self.push_repeat(node, count + 1, pos, cont);
            return self.match_node(body, pos, resume);
        }
        if (may_repeat) {
            let resume = self.push_repeat(node, count + 1, pos, cont);
            let more = self.match_node(body, pos, resume);
            if (more != NONE) {
                return more;
            }
            if (self.exhausted()) {
                return NONE;
            }
        }
        if (may_continue) {
            return self.run_continuation(cont, pos);
        }
        return NONE;
    }

    // Consume iterations with an empty continuation until the body stops
    // matching, then hand the final position to the real continuation.  No
    // iteration is ever undone, which is the defining property.
    fn match_possessive(ref self, node: int, pos: int, count: int, cont: int) -> int {
        let lower = self.nodes[node].lower;
        let upper = self.nodes[node].upper;
        let body = self.nodes[node].first;
        var taken = count;
        var cursor = pos;
        while (true) {
            if (upper != NONE) {
                if (taken >= upper) {
                    break;
                }
            }
            let advanced = self.match_node(body, cursor, NONE);
            if (advanced == NONE) {
                break;
            }
            if (advanced == cursor) {
                // An empty iteration adds nothing and would loop forever.
                break;
            }
            cursor = advanced;
            taken += 1;
            if (self.exhausted()) {
                return NONE;
            }
        }
        if (taken < lower) {
            return NONE;
        }
        return self.run_continuation(cont, cursor);
    }

    // --- Public entry points ----------------------------------------------

    fn match_at(ref self, start: int) -> bool {
        self.reset();
        let end = self.match_node(self.root, start, NONE);
        if (end == NONE) {
            if (not self.exhausted()) {
                self.status = MatchStatus.NoMatch;
            }
            return false;
        }
        self.status = MatchStatus.Matched;
        let ref whole: auto = self.captures[0];
        whole.begin = start;
        whole.end = end;
        return true;
    }

    fn search_from(ref self, from: int) -> bool {
        var start = from;
        while (start <= self.text.size()) {
            if (self.match_at(start)) {
                return true;
            }
            if (self.exhausted()) {
                return false;
            }
            start += 1;
        }
        return false;
    }

    fn match_whole(ref self) -> bool {
        if (not self.match_at(0)) {
            return false;
        }
        let ref whole: auto = self.captures[0];
        return whole.end == self.text.size();
    }

    var nodes: vector(Node);
    var names: vector(GroupName);
    var frames: vector(ContFrame);
    var captures: vector(Capture);
    var text: string;
    var root: int = -1;
    var group_count: int = 0;
    var steps: int = 0;
    var budget: int = 1000000;
    var status: MatchStatus = MatchStatus.NoMatch;
    var ignore_case: bool = false;
    var multiline: bool = false;
}

// --- Compiled program ----------------------------------------------------
//
// The third representation, after the AST and the NFA.  A program is a flat
// instruction array in which control flow is expressed by absolute indices,
// which makes it cheap to store, cheap to copy and easy to rewrite - the
// optimiser below works on programs, not on trees.
//
// The instruction set is deliberately small.  OpString and OpClassSet do not
// add expressive power; they exist so the optimiser has something to fuse
// runs of OpChar and adjacent OpClass instructions into.

choice OpCode {
    OpChar,             // consume one specific character
    OpString,           // consume a fixed run of characters
    OpClass,            // consume one character of a class
    OpAny,              // consume any character
    OpSplit,            // try `x`, then `y`
    OpJump,             // continue at `x`
    OpSave,             // record the offset in capture slot `slot`
    OpAssert,           // zero width assertion
    OpBackref,          // re-match capture group `slot`
    OpMatch,            // accept
    OpFail,        // never matches; left behind by the optimiser
}

class Instruction {

    fn consumes_input(self) -> bool {
        if (self.op == OpCode.OpChar) {
            return true;
        }
        if (self.op == OpCode.OpString) {
            return true;
        }
        if (self.op == OpCode.OpClass) {
            return true;
        }
        if (self.op == OpCode.OpAny) {
            return true;
        }
        return false;
    }

    fn is_branch(self) -> bool {
        if (self.op == OpCode.OpSplit) {
            return true;
        }
        if (self.op == OpCode.OpJump) {
            return true;
        }
        return false;
    }

    fn is_terminal(self) -> bool {
        if (self.op == OpCode.OpMatch) {
            return true;
        }
        return self.op == OpCode.OpFail;
    }

    // How much input the instruction always consumes.  OpString reports its
    // whole length, which is what the prefix scanner wants.
    fn minimum_width(self) -> int {
        if (self.op == OpCode.OpString) {
            return self.text.size();
        }
        if (self.consumes_input()) {
            return 1;
        }
        return 0;
    }

    var op: OpCode = OpCode.OpFail;
    var value: char = '\0';
    var text: string;
    var charclass: CharClass;
    var anchor: AnchorKind = AnchorKind.AnchorLineStart;
    var slot: int = 0;
    var x: int = -1;
    var y: int = -1;
}

class Program {

    fn size(self) -> int {
        return self.code.size();
    }

    fn empty(self) -> bool {
        return self.code.empty();
    }

    fn at(ref self, pc: int) -> ref Instruction {
        return self.code[pc];
    }

    fn op_at(self, pc: int) -> OpCode {
        if (pc < 0) {
            return OpCode.OpFail;
        }
        if (pc >= self.code.size()) {
            return OpCode.OpFail;
        }
        return self.code[pc].op;
    }

    fn emit(ref self, inst: Instruction) -> int {
        let at = self.code.size();
        self.code.push_back(inst);
        return at;
    }

    fn emit_op(ref self, op: OpCode) -> int {
        return self.emit(Instruction({.op = op}));
    }

    fn emit_char(ref self, c: char) -> int {
        var inst = Instruction({.op = OpCode.OpChar, .value = c});
        inst.charclass.add_char(c);
        return self.emit(inst);
    }

    fn emit_class(ref self, cls: CharClass) -> int {
        var inst = Instruction({.op = OpCode.OpClass});
        inst.charclass = cls;
        inst.charclass.normalise();
        return self.emit(inst);
    }

    fn emit_save(ref self, slot: int) -> int {
        var inst = Instruction({.op = OpCode.OpSave});
        inst.slot = slot;
        return self.emit(inst);
    }

    fn emit_assert(ref self, which: AnchorKind) -> int {
        var inst = Instruction({.op = OpCode.OpAssert});
        inst.anchor = which;
        return self.emit(inst);
    }

    fn emit_backref(ref self, slot: int) -> int {
        var inst = Instruction({.op = OpCode.OpBackref});
        inst.slot = slot;
        return self.emit(inst);
    }

    fn set_target(ref self, pc: int, target: int) {
        let ref inst: auto = self.code[pc];
        inst.x = target;
    }

    fn set_alternate(ref self, pc: int, target: int) {
        let ref inst: auto = self.code[pc];
        inst.y = target;
    }

    fn instruction_count_of(self, op: OpCode) -> int {
        var total: int = 0;
        for (inst in self.code) {
            if (inst.op == op) {
                total += 1;
            }
        }
        return total;
    }

    // Shortest possible match, used to reject subjects that are too short
    // before any real work happens.  Splits take the smaller of the two
    // arms, which is a lower bound and therefore always safe.
    fn minimum_length(self) -> int {
        var total: int = 0;
        for (inst in self.code) {
            if (inst.op == OpCode.OpSplit) {
                return total;
            }
            total += inst.minimum_width();
            if (inst.op == OpCode.OpMatch) {
                break;
            }
        }
        return total;
    }

    fn disassemble(self) -> string {
        var out = TEXT_EMPTY;
        var pc: int = 0;
        while (pc < self.code.size()) {
            let ref inst: auto = self.code[pc];
            out.append(to_string(pc));
            out.append(TEXT_SPACE_ARROW);
            out.append(opcode_name(inst.op));
            if (inst.op == OpCode.OpChar) {
                out.append(TEXT_SPACE);
                out.append(escape_spelling(inst.value));
            }
            if (inst.op == OpCode.OpString) {
                out.append(TEXT_SPACE);
                out.append(inst.text);
            }
            if (inst.op == OpCode.OpClass) {
                out.append(TEXT_SPACE);
                out.append(inst.charclass.spell());
            }
            if (inst.is_branch()) {
                out.append(TEXT_SPACE);
                out.append(to_string(inst.x));
            }
            if (inst.op == OpCode.OpSplit) {
                out.append(TEXT_COMMA_SPACE);
                out.append(to_string(inst.y));
            }
            out.append(TEXT_NEWLINE);
            pc += 1;
        }
        return out;
    }

    var code: vector(Instruction);
    var names: vector(GroupName);
    var prefix: string;
    var group_count: int = 0;
    var start: int = 0;
    var anchored: bool = false;
    var has_backreference: bool = false;
    var ignore_case: bool = false;
    var multiline: bool = false;
    var dot_matches_newline: bool = false;
}

let TEXT_SPACE: string = " ";
let TEXT_NEWLINE: string = "\n";
let TEXT_COMMA_SPACE: string = ", ";
let TEXT_SPACE_ARROW: string = ": ";

fn opcode_name(op: OpCode) -> string {
    if (op == OpCode.OpChar) {
        return "char";
    }
    if (op == OpCode.OpString) {
        return "string";
    }
    if (op == OpCode.OpClass) {
        return "class";
    }
    if (op == OpCode.OpAny) {
        return "any";
    }
    if (op == OpCode.OpSplit) {
        return "split";
    }
    if (op == OpCode.OpJump) {
        return "jump";
    }
    if (op == OpCode.OpSave) {
        return "save";
    }
    if (op == OpCode.OpAssert) {
        return "assert_at";
    }
    if (op == OpCode.OpBackref) {
        return "backref";
    }
    if (op == OpCode.OpMatch) {
        return "match";
    }
    return "fail";
}

// --- Compiler ------------------------------------------------------------
//
// A straight recursive walk of the AST, emitting instructions as it goes.
// Because targets are absolute indices and a forward branch does not know
// where it lands, the compiler emits a placeholder and patches it once the
// destination is known - the same hole-and-patch discipline the NFA builder
// uses, expressed in indices instead of exit lists.

class Compiler {

    fn compile(ref self, parser: PatternParser) -> bool {
        self.program = Program();
        self.nodes = parser.nodes;
        self.program.group_count = parser.group_count;
        self.program.names = parser.names;
        self.ok = true;
        Discard(self.program.emit_save(0));
        self.compile_node(parser.root);
        if (not self.ok) {
            return false;
        }
        Discard(self.program.emit_save(1));
        Discard(self.program.emit_op(OpCode.OpMatch));
        self.program.anchored = self.starts_anchored(parser.root);
        return true;
    }

    fn here(self) -> int {
        return self.program.code.size();
    }

    fn compile_node(ref self, index: int) {
        if (index == NONE) {
            return;
        }
        if (not self.ok) {
            return;
        }
        let kind = self.nodes[index].kind;
        if (kind == NodeKind.NodeEmpty) {
            return;
        }
        if (kind == NodeKind.NodeLiteral) {
            Discard(self.program.emit_char(self.nodes[index].value));
            return;
        }
        if (kind == NodeKind.NodeString) {
            var inst = Instruction({.op = OpCode.OpString});
            inst.text = self.nodes[index].text;
            Discard(self.program.emit(inst));
            return;
        }
        if (kind == NodeKind.NodeAny) {
            Discard(self.program.emit_class(self.nodes[index].charclass));
            return;
        }
        if (kind == NodeKind.NodeClass) {
            Discard(self.program.emit_class(self.nodes[index].charclass));
            return;
        }
        if (kind == NodeKind.NodeAnchor) {
            Discard(self.program.emit_assert(self.nodes[index].anchor));
            return;
        }
        if (kind == NodeKind.NodeBackreference) {
            self.program.has_backreference = true;
            Discard(self.program.emit_backref(self.nodes[index].reference));
            return;
        }
        if (kind == NodeKind.NodeConcat) {
            self.compile_concat(index);
            return;
        }
        if (kind == NodeKind.NodeAlternate) {
            self.compile_alternate(index);
            return;
        }
        if (kind == NodeKind.NodeGroup) {
            self.compile_group(index);
            return;
        }
        if (kind == NodeKind.NodeRepeat) {
            self.compile_repeat(index);
            return;
        }
        self.ok = false;
    }

    fn compile_concat(ref self, index: int) {
        let count = self.nodes[index].children.size();
        var i: int = 0;
        while (i < count) {
            self.compile_node(self.nodes[index].children[i]);
            if (not self.ok) {
                return;
            }
            i += 1;
        }
    }

    // Each branch but the last is guarded by a split and followed by a jump
    // to the join point.  The jumps are collected and patched together once
    // the last branch has been emitted.
    fn compile_alternate(ref self, index: int) {
        let count = self.nodes[index].children.size();
        if (count == 0) {
            return;
        }
        var jumps = vector(int)();
        var i: int = 0;
        while (i < count) {
            let child = self.nodes[index].children[i];
            if (i + 1 == count) {
                self.compile_node(child);
                i += 1;
                continue;
            }
            let split = self.program.emit_op(OpCode.OpSplit);
            self.program.set_target(split, self.here());
            self.compile_node(child);
            if (not self.ok) {
                return;
            }
            let jump = self.program.emit_op(OpCode.OpJump);
            jumps.push_back(jump);
            self.program.set_alternate(split, self.here());
            i += 1;
        }
        let join = self.here();
        for (jump in jumps) {
            self.program.set_target(jump, join);
        }
    }

    fn compile_group(ref self, index: int) {
        let group = self.nodes[index].group_index;
        if (group == NONE) {
            self.compile_node(self.nodes[index].first);
            return;
        }
        Discard(self.program.emit_save(group * 2));
        self.compile_node(self.nodes[index].first);
        if (not self.ok) {
            return;
        }
        Discard(self.program.emit_save(group * 2 + 1));
    }

    fn compile_repeat(ref self, index: int) {
        let lower = self.nodes[index].lower;
        let upper = self.nodes[index].upper;
        let body = self.nodes[index].first;
        let lazy = self.nodes[index].greed == Greediness.Lazy;
        var i: int = 0;
        while (i < lower) {
            self.compile_node(body);
            if (not self.ok) {
                return;
            }
            i += 1;
        }
        if (upper == NONE) {
            self.compile_star(body, lazy);
            return;
        }
        var extra = upper - lower;
        var splits = vector(int)();
        while (extra > 0) {
            let split = self.program.emit_op(OpCode.OpSplit);
            splits.push_back(split);
            if (lazy) {
                self.program.set_alternate(split, self.here());
            } else {
                self.program.set_target(split, self.here());
            }
            self.compile_node(body);
            if (not self.ok) {
                return;
            }
            extra -= 1;
        }
        let after = self.here();
        for (split in splits) {
            if (lazy) {
                self.program.set_target(split, after);
            } else {
                self.program.set_alternate(split, after);
            }
        }
    }

    // The star loop: split, body, jump back, and the split's other arm points
    // past the jump.  Swapping the arms turns the loop lazy.
    fn compile_star(ref self, body: int, lazy: bool) {
        let split = self.program.emit_op(OpCode.OpSplit);
        let body_at = self.here();
        self.compile_node(body);
        if (not self.ok) {
            return;
        }
        let jump = self.program.emit_op(OpCode.OpJump);
        self.program.set_target(jump, split);
        let after = self.here();
        if (lazy) {
            self.program.set_target(split, after);
            self.program.set_alternate(split, body_at);
        } else {
            self.program.set_target(split, body_at);
            self.program.set_alternate(split, after);
        }
    }

    // True when every path through the pattern begins with an anchor that
    // pins it to the start of the subject.  Such a program never needs to be
    // retried at later offsets.
    fn starts_anchored(self, index: int) -> bool {
        if (index == NONE) {
            return false;
        }
        let kind = self.nodes[index].kind;
        if (kind == NodeKind.NodeAnchor) {
            let which = self.nodes[index].anchor;
            if (which == AnchorKind.AnchorTextStart) {
                return true;
            }
            return which == AnchorKind.AnchorLineStart;
        }
        if (kind == NodeKind.NodeGroup) {
            return self.starts_anchored(self.nodes[index].first);
        }
        if (kind == NodeKind.NodeConcat) {
            if (self.nodes[index].children.empty()) {
                return false;
            }
            return self.starts_anchored(self.nodes[index].children[0]);
        }
        if (kind == NodeKind.NodeAlternate) {
            // Every branch must be anchored, or the pattern can still match
            // somewhere in the middle of the subject.
            if (self.nodes[index].children.empty()) {
                return false;
            }
            for (child in self.nodes[index].children) {
                if (not self.starts_anchored(child)) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    var program: Program;
    var nodes: vector(Node);
    var ok: bool = true;
}

// --- Optimiser -----------------------------------------------------------
//
// Two families of rewrites, both optional: nothing here changes what a
// pattern matches, only how much work matching it costs.
//
// On the tree:
//   * runs of adjacent literals become one NodeString
//   * an alternation of single character branches becomes one NodeClass
//   * degenerate repetitions - {1,1}, {0,0} - collapse
//
// On the program:
//   * a jump to a jump is threaded through to its final destination
//   * instructions no branch can reach are replaced with OpFail
//
// Every pass reports how many rewrites it made, and the driver repeats the
// set until a round changes nothing or the budget runs out.

class OptimiserStats {

    fn total(self) -> int {
        var sum = self.literal_runs + self.merged_classes;
        sum += self.collapsed_repeats;
        sum += self.threaded_jumps;
        sum += self.dead_instructions;
        return sum;
    }

    fn describe(self) -> string {
        var out = TEXT_EMPTY;
        out.append("literal runs: ");
        out.append(to_string(self.literal_runs));
        out.append(TEXT_COMMA_SPACE);
        out.append("merged classes: ");
        out.append(to_string(self.merged_classes));
        out.append(TEXT_COMMA_SPACE);
        out.append("collapsed repeats: ");
        out.append(to_string(self.collapsed_repeats));
        out.append(TEXT_COMMA_SPACE);
        out.append("threaded jumps: ");
        out.append(to_string(self.threaded_jumps));
        out.append(TEXT_COMMA_SPACE);
        out.append("dead instructions: ");
        out.append(to_string(self.dead_instructions));
        return out;
    }

    var literal_runs: int = 0;
    var merged_classes: int = 0;
    var collapsed_repeats: int = 0;
    var threaded_jumps: int = 0;
    var dead_instructions: int = 0;
    var rounds: int = 0;
}

class Optimiser {

    fn changed(self) -> bool {
        return self.rewrites > 0;
    }

    fn spend(ref self) -> bool {
        self.work += 1;
        return self.work <= OPTIMISER_BUDGET;
    }

    // --- Tree passes ------------------------------------------------------

    fn optimise_tree(ref self, ref parser: PatternParser) -> int {
        self.rewrites = 0;
        var round: int = 0;
        while (round < 4) {
            let before = self.rewrites;
            self.fuse_literals(parser, parser.root);
            self.merge_alternate_classes(parser, parser.root);
            self.collapse_repeats(parser, parser.root);
            self.stats.rounds += 1;
            if (self.rewrites == before) {
                break;
            }
            round += 1;
        }
        return self.rewrites;
    }

    // Walk the tree bottom up, replacing every run of two or more adjacent
    // NodeLiteral children of a concatenation with a single NodeString.  A
    // run of one is left alone: OpChar is already the cheapest form.
    fn fuse_literals(ref self, ref parser: PatternParser, index: int) {
        if (index == NONE) {
            return;
        }
        if (not self.spend()) {
            return;
        }
        let kind = parser.nodes[index].kind;
        if (kind == NodeKind.NodeRepeat) {
            self.fuse_literals(parser, parser.nodes[index].first);
            return;
        }
        if (kind == NodeKind.NodeGroup) {
            self.fuse_literals(parser, parser.nodes[index].first);
            return;
        }
        if (kind == NodeKind.NodeAlternate) {
            let count = parser.nodes[index].children.size();
            var i: int = 0;
            while (i < count) {
                self.fuse_literals(parser, parser.nodes[index].children[i]);
                i += 1;
            }
            return;
        }
        if (kind != NodeKind.NodeConcat) {
            return;
        }
        let count = parser.nodes[index].children.size();
        var i: int = 0;
        while (i < count) {
            self.fuse_literals(parser, parser.nodes[index].children[i]);
            i += 1;
        }
        var rebuilt = vector(int)();
        var run = TEXT_EMPTY;
        var run_start = NONE;
        i = 0;
        while (i < count) {
            let child = parser.nodes[index].children[i];
            let child_kind = parser.nodes[child].kind;
            var literal = child_kind == NodeKind.NodeLiteral;
            if (literal and i + 1 < count) {
                // A literal followed by a quantifier must stay separate: the
                // quantifier binds to that one character, not to the run.
                let next = parser.nodes[index].children[i + 1];
                if (parser.nodes[next].kind == NodeKind.NodeRepeat) {
                    if (parser.nodes[next].first == child) {
                        literal = false;
                    }
                }
            }
            if (literal) {
                if (run_start == NONE) {
                    run_start = child;
                }
                run.push_back(parser.nodes[child].value);
            } else {
                if (run.size() >= 2) {
                    let fused = self.make_string_node(parser, run);
                    rebuilt.push_back(fused);
                    self.rewrites += 1;
                    self.stats.literal_runs += 1;
                } else {
                    if (run_start != NONE) {
                        rebuilt.push_back(run_start);
                    }
                }
                run = TEXT_EMPTY;
                run_start = NONE;
                rebuilt.push_back(child);
            }
            i += 1;
        }
        if (run.size() >= 2) {
            let fused = self.make_string_node(parser, run);
            rebuilt.push_back(fused);
            self.rewrites += 1;
            self.stats.literal_runs += 1;
        } else {
            if (run_start != NONE) {
                rebuilt.push_back(run_start);
            }
        }
        let ref n: auto = parser.nodes[index];
        n.children = move(rebuilt);
    }

    fn make_string_node(ref self, ref parser: PatternParser, text: string) -> int {
        let index = parser.nodes.size();
        var n = Node({.kind = NodeKind.NodeString});
        n.text = text;
        parser.nodes.push_back(n);
        return index;
    }

    // `a|b|c` and `[ab]|[cd]` both describe a set of single characters, and a
    // single class test is far cheaper than a chain of splits.  The rewrite
    // only fires when *every* branch is a single character or a class, since
    // a mixed alternation still needs its control flow.
    fn merge_alternate_classes(ref self, ref parser: PatternParser, index: int) {
        if (index == NONE) {
            return;
        }
        if (not self.spend()) {
            return;
        }
        let kind = parser.nodes[index].kind;
        if (kind == NodeKind.NodeRepeat) {
            self.merge_alternate_classes(parser, parser.nodes[index].first);
            return;
        }
        if (kind == NodeKind.NodeGroup) {
            self.merge_alternate_classes(parser, parser.nodes[index].first);
            return;
        }
        if (kind == NodeKind.NodeConcat) {
            let count = parser.nodes[index].children.size();
            var i: int = 0;
            while (i < count) {
                self.merge_alternate_classes(parser, parser.nodes[index].children[i]);
                i += 1;
            }
            return;
        }
        if (kind != NodeKind.NodeAlternate) {
            return;
        }
        let count = parser.nodes[index].children.size();
        var i: int = 0;
        while (i < count) {
            self.merge_alternate_classes(parser, parser.nodes[index].children[i]);
            i += 1;
        }
        if (count < 2) {
            return;
        }
        var mergeable = true;
        i = 0;
        while (i < count) {
            let child = parser.nodes[index].children[i];
            let child_kind = parser.nodes[child].kind;
            var fine = false;
            if (child_kind == NodeKind.NodeLiteral) {
                fine = true;
            }
            if (child_kind == NodeKind.NodeClass) {
                // A negated class cannot be unioned by concatenating ranges;
                // it would have to be complemented first, and that is not
                // worth doing inside a pass meant to be cheap.
                if (not parser.nodes[child].charclass.negated) {
                    fine = true;
                }
            }
            if (not fine) {
                mergeable = false;
                break;
            }
            i += 1;
        }
        if (not mergeable) {
            return;
        }
        var merged = CharClass();
        i = 0;
        while (i < count) {
            let child = parser.nodes[index].children[i];
            merged.add_class(parser.nodes[child].charclass);
            i += 1;
        }
        merged.normalise();
        let ref n: auto = parser.nodes[index];
        n.kind = NodeKind.NodeClass;
        n.charclass = merged;
        n.children.clear();
        self.rewrites += 1;
        self.stats.merged_classes += 1;
    }

    // `e{1,1}` is `e`, and `e{0,0}` is the empty string.  Both turn up
    // constantly in generated patterns.
    fn collapse_repeats(ref self, ref parser: PatternParser, index: int) {
        if (index == NONE) {
            return;
        }
        if (not self.spend()) {
            return;
        }
        let kind = parser.nodes[index].kind;
        if (kind == NodeKind.NodeConcat) {
            let count = parser.nodes[index].children.size();
            var i: int = 0;
            while (i < count) {
                self.collapse_repeats(parser, parser.nodes[index].children[i]);
                i += 1;
            }
            return;
        }
        if (kind == NodeKind.NodeAlternate) {
            let count = parser.nodes[index].children.size();
            var i: int = 0;
            while (i < count) {
                self.collapse_repeats(parser, parser.nodes[index].children[i]);
                i += 1;
            }
            return;
        }
        if (kind == NodeKind.NodeGroup) {
            self.collapse_repeats(parser, parser.nodes[index].first);
            return;
        }
        if (kind != NodeKind.NodeRepeat) {
            return;
        }
        self.collapse_repeats(parser, parser.nodes[index].first);
        let lower = parser.nodes[index].lower;
        let upper = parser.nodes[index].upper;
        if (lower == 0 and upper == 0) {
            let ref n: auto = parser.nodes[index];
            n.kind = NodeKind.NodeEmpty;
            n.first = NONE;
            self.rewrites += 1;
            self.stats.collapsed_repeats += 1;
            return;
        }
        if (lower == 1 and upper == 1) {
            let body = parser.nodes[index].first;
            let ref source: auto = parser.nodes[body];
            var copy = source;
            let ref n: auto = parser.nodes[index];
            n.kind = copy.kind;
            n.value = copy.value;
            n.text = copy.text;
            n.charclass = copy.charclass;
            n.anchor = copy.anchor;
            n.group_kind = copy.group_kind;
            n.group_index = copy.group_index;
            n.reference = copy.reference;
            n.lower = copy.lower;
            n.upper = copy.upper;
            n.first = copy.first;
            n.children = copy.children;
            self.rewrites += 1;
            self.stats.collapsed_repeats += 1;
        }
    }

    // --- Literal prefix extraction ----------------------------------------
    //
    // Walk the front of the pattern collecting characters that every match
    // must begin with.  A search can then skip to the next occurrence of that
    // prefix instead of retrying the whole machine at every offset, which is
    // the single most valuable optimisation for patterns like `error: .*`.

    fn extract_prefix(ref self, parser: PatternParser, index: int) -> string {
        var out = TEXT_EMPTY;
        self.collect_prefix(parser, index, out);
        return out;
    }

    fn collect_prefix(ref self, parser: PatternParser, index: int, ref out: string) {
        if (index == NONE) {
            return;
        }
        if (not self.spend()) {
            return;
        }
        let kind = parser.nodes[index].kind;
        if (kind == NodeKind.NodeLiteral) {
            out.push_back(parser.nodes[index].value);
            return;
        }
        if (kind == NodeKind.NodeString) {
            out.append(parser.nodes[index].text);
            return;
        }
        if (kind == NodeKind.NodeGroup) {
            if (parser.nodes[index].group_kind == GroupKind.NonCapturing) {
                self.collect_prefix(parser, parser.nodes[index].first, out);
            }
            return;
        }
        if (kind == NodeKind.NodeAnchor) {
            // Anchors consume nothing, so the prefix continues past them.
            return;
        }
        if (kind == NodeKind.NodeRepeat) {
            if (parser.nodes[index].lower >= 1) {
                // The first mandatory iteration contributes its own prefix,
                // but nothing after it is certain.
                var inner = TEXT_EMPTY;
                self.collect_prefix(parser, parser.nodes[index].first, inner);
                out.append(inner);
            }
            return;
        }
        if (kind != NodeKind.NodeConcat) {
            return;
        }
        let count = parser.nodes[index].children.size();
        var i: int = 0;
        while (i < count) {
            let child = parser.nodes[index].children[i];
            let child_kind = parser.nodes[child].kind;
            if (child_kind == NodeKind.NodeLiteral) {
                out.push_back(parser.nodes[child].value);
                i += 1;
                continue;
            }
            if (child_kind == NodeKind.NodeString) {
                out.append(parser.nodes[child].text);
                i += 1;
                continue;
            }
            if (child_kind == NodeKind.NodeAnchor) {
                i += 1;
                continue;
            }
            break;
        }
    }

    // --- Program passes ---------------------------------------------------

    fn optimise_program(ref self, ref program: Program) -> int {
        self.rewrites = 0;
        var round: int = 0;
        while (round < 3) {
            let before = self.rewrites;
            self.thread_jumps(program);
            self.mark_dead(program);
            if (self.rewrites == before) {
                break;
            }
            round += 1;
        }
        return self.rewrites;
    }

    // A branch that lands on an unconditional jump can point at the jump's
    // own destination instead.  Chains are followed with a hop limit so a
    // loop of jumps - which the compiler cannot emit, but a rewritten program
    // might - cannot spin forever.
    fn thread_jumps(ref self, ref program: Program) {
        var pc: int = 0;
        while (pc < program.code.size()) {
            if (not self.spend()) {
                return;
            }
            let ref inst: auto = program.code[pc];
            if (inst.is_branch()) {
                let target = self.final_target(program, inst.x);
                if (target != inst.x) {
                    program.set_target(pc, target);
                    self.rewrites += 1;
                    self.stats.threaded_jumps += 1;
                }
            }
            let ref again: auto = program.code[pc];
            if (again.op == OpCode.OpSplit) {
                let target = self.final_target(program, again.y);
                if (target != again.y) {
                    program.set_alternate(pc, target);
                    self.rewrites += 1;
                    self.stats.threaded_jumps += 1;
                }
            }
            pc += 1;
        }
    }

    fn final_target(ref self, program: Program, start: int) -> int {
        var target = start;
        var hops: int = 0;
        while (hops < 32) {
            if (target < 0) {
                return start;
            }
            if (target >= program.code.size()) {
                return start;
            }
            let ref inst: auto = program.code[target];
            if (inst.op != OpCode.OpJump) {
                return target;
            }
            if (inst.x == target) {
                return target;
            }
            target = inst.x;
            hops += 1;
        }
        return target;
    }

    // Reachability from the entry point.  Anything unvisited is replaced with
    // OpFail, which keeps the indices stable - renumbering would mean
    // rewriting every branch in the program.
    fn mark_dead(ref self, ref program: Program) {
        var reachable = vector(bool)();
        reachable.resize(program.code.size());
        var i: int = 0;
        while (i < program.code.size()) {
            reachable[i] = false;
            i += 1;
        }
        var stack = vector(int)();
        stack.push_back(program.start);
        while (not stack.empty()) {
            if (not self.spend()) {
                return;
            }
            let pc = stack.back();
            stack.pop_back();
            if (pc < 0) {
                continue;
            }
            if (pc >= program.code.size()) {
                continue;
            }
            if (reachable[pc]) {
                continue;
            }
            reachable[pc] = true;
            let ref inst: auto = program.code[pc];
            if (inst.op == OpCode.OpMatch) {
                continue;
            }
            if (inst.op == OpCode.OpFail) {
                continue;
            }
            if (inst.op == OpCode.OpJump) {
                stack.push_back(inst.x);
                continue;
            }
            if (inst.op == OpCode.OpSplit) {
                stack.push_back(inst.x);
                stack.push_back(inst.y);
                continue;
            }
            stack.push_back(pc + 1);
        }
        i = 0;
        while (i < program.code.size()) {
            if (not reachable[i]) {
                let ref inst: auto = program.code[i];
                if (inst.op != OpCode.OpFail) {
                    inst.op = OpCode.OpFail;
                    self.rewrites += 1;
                    self.stats.dead_instructions += 1;
                }
            }
            i += 1;
        }
    }

    var stats: OptimiserStats;
    var rewrites: int = 0;
    var work: int = 0;
}

// --- Match results -------------------------------------------------------

class MatchResult {

    fn matched(self) -> bool {
        return self.found;
    }

    fn begin(self) -> int {
        let ref whole: auto = self.groups[0];
        return whole.begin;
    }

    fn end(self) -> int {
        let ref whole: auto = self.groups[0];
        return whole.end;
    }

    fn length(self) -> int {
        return self.end() - self.begin();
    }

    fn group_count(self) -> int {
        return self.groups.size();
    }

    fn group(self, index: int) -> string {
        if (index < 0) {
            return TEXT_EMPTY;
        }
        if (index >= self.groups.size()) {
            return TEXT_EMPTY;
        }
        let ref cap: auto = self.groups[index];
        return cap.text_of(self.subject);
    }

    fn group_begin(self, index: int) -> int {
        if (index >= self.groups.size()) {
            return NONE;
        }
        let ref cap: auto = self.groups[index];
        return cap.begin;
    }

    fn has_group(self, index: int) -> bool {
        if (index >= self.groups.size()) {
            return false;
        }
        let ref cap: auto = self.groups[index];
        return cap.valid();
    }

    fn text(self) -> string {
        return self.group(0);
    }

    fn prefix_text(self) -> string {
        return self.subject.substr(0, self.begin());
    }

    fn suffix_text(self) -> string {
        let from = self.end();
        return self.subject.substr(from, self.subject.size() - from);
    }

    var subject: string;
    var groups: vector(Capture);
    var names: vector(GroupName);
    var found: bool = false;
    var steps: int = 0;
    var status: MatchStatus = MatchStatus.NoMatch;
}

fn empty_result(subject: string) -> MatchResult {
    var r = MatchResult();
    r.subject = subject;
    r.groups.push_back(Capture());
    return r;
}

// --- The engine facade ---------------------------------------------------
//
// `Regex` owns every representation at once: the AST for the backtracker, the
// NFA for the simulator and the compiled program for inspection.  Which one
// runs is decided per call - a pattern with a backreference or a capture
// request has to backtrack, anything else can be simulated in linear time.

choice EngineChoice {
    ChooseAutomatic,
    ForceBacktrack,
    ForceSimulation,
}

class Regex {

    fn valid(self) -> bool {
        return self.ok;
    }

    fn error_text(self) -> string {
        return self.error;
    }

    fn error_offset(self) -> int {
        return self.error_position;
    }

    fn group_count(self) -> int {
        return self.parser.group_count;
    }

    fn pattern_text(self) -> string {
        return self.pattern;
    }

    fn program_size(self) -> int {
        return self.compiler.program.size();
    }

    fn state_count(self) -> int {
        return self.builder.state_count();
    }

    fn literal_prefix(self) -> string {
        return self.prefix;
    }

    // --- Compilation ------------------------------------------------------

    fn compile(ref self, pattern: string) -> bool {
        self.pattern = pattern;
        self.ok = true;
        self.error = ERROR_NONE;
        self.parser = PatternParser();
        self.parser.dot_matches_newline = self.dot_matches_newline;
        self.parser.ignore_case = self.ignore_case;
        let root = self.parser.parse(pattern);
        if (root == NONE) {
            self.ok = false;
            self.error = self.parser.error;
            self.error_position = self.parser.error_position;
            return false;
        }
        self.optimiser = Optimiser();
        Discard(self.optimiser.optimise_tree(self.parser));
        self.prefix = self.optimiser.extract_prefix(self.parser, self.parser.root);
        if (not self.builder.build(self.parser, self.parser.root)) {
            self.ok = false;
            self.error = ERROR_BAD_GROUP;
            return false;
        }
        if (not self.compiler.compile(self.parser)) {
            self.ok = false;
            self.error = ERROR_BAD_GROUP;
            return false;
        }
        Discard(self.optimiser.optimise_program(self.compiler.program));
        self.compiler.program.prefix = self.prefix;
        self.compiler.program.ignore_case = self.ignore_case;
        self.compiler.program.multiline = self.multiline;
        self.anchored = self.compiler.program.anchored;
        return true;
    }

    fn prepare_backtracker(ref self, subject: string) {
        self.matcher = BacktrackMatcher();
        self.matcher.load(self.parser);
        self.matcher.text = subject;
        self.matcher.ignore_case = self.ignore_case;
        self.matcher.multiline = self.multiline;
        self.matcher.budget = BACKTRACK_BUDGET;
    }

    fn prepare_simulator(ref self, subject: string) {
        self.simulator = NfaSimulator();
        self.simulator.load(self.builder);
        self.simulator.text = subject;
        self.simulator.ignore_case = self.ignore_case;
        self.simulator.multiline = self.multiline;
    }

    // The simulator is only usable when the caller does not need captures and
    // the pattern has no backreference.
    fn may_simulate(self, want_captures: bool) -> bool {
        if (self.engine == EngineChoice.ForceBacktrack) {
            return false;
        }
        if (self.engine == EngineChoice.ForceSimulation) {
            return true;
        }
        if (self.builder.has_backreference) {
            return false;
        }
        if (want_captures) {
            return false;
        }
        return true;
    }

    // --- Prefix acceleration ----------------------------------------------
    //
    // Find the next offset at or after `from` where the literal prefix occurs.
    // With no prefix every offset qualifies, so the scan degenerates to the
    // naive one.

    fn next_candidate(self, subject: string, from: int) -> int {
        if (self.prefix.empty()) {
            return from;
        }
        var start = from;
        let limit = subject.size() - self.prefix.size();
        while (start <= limit) {
            var i: int = 0;
            var hit = true;
            while (i < self.prefix.size()) {
                let expected = self.prefix.at(i);
                let actual = subject.at(start + i);
                if (self.ignore_case) {
                    if (not same_char_folded(expected, actual)) {
                        hit = false;
                        break;
                    }
                } else {
                    if (expected != actual) {
                        hit = false;
                        break;
                    }
                }
                i += 1;
            }
            if (hit) {
                return start;
            }
            start += 1;
        }
        return NONE;
    }

    // --- match ------------------------------------------------------------
    //
    // Anchored at both ends: the pattern must describe the whole subject.

    fn match_text(ref self, subject: string) -> MatchResult {
        var result = empty_result(subject);
        if (not self.ok) {
            return result;
        }
        self.prepare_backtracker(subject);
        if (not self.matcher.match_whole()) {
            result.status = self.matcher.status;
            result.steps = self.matcher.steps;
            return result;
        }
        result.found = true;
        result.groups = self.matcher.captures;
        result.names = self.parser.names;
        result.steps = self.matcher.steps;
        result.status = MatchStatus.Matched;
        return result;
    }

    // --- search -----------------------------------------------------------
    //
    // Leftmost match anywhere in the subject.

    fn search(ref self, subject: string) -> MatchResult {
        return self.search_from(subject, 0);
    }

    fn search_from(ref self, subject: string, from: int) -> MatchResult {
        var result = empty_result(subject);
        if (not self.ok) {
            return result;
        }
        if (from > subject.size()) {
            return result;
        }
        self.prepare_backtracker(subject);
        var start = from;
        while (true) {
            if (start > subject.size()) {
                break;
            }
            let candidate = self.next_candidate(subject, start);
            if (candidate == NONE) {
                break;
            }
            if (self.matcher.match_at(candidate)) {
                result.found = true;
                result.groups = self.matcher.captures;
                result.names = self.parser.names;
                result.steps = self.matcher.steps;
                result.status = MatchStatus.Matched;
                return result;
            }
            if (self.matcher.exhausted()) {
                result.status = MatchStatus.Exhausted;
                return result;
            }
            if (self.anchored) {
                // An anchored pattern that failed at the first candidate can
                // never succeed later, unless multiline mode makes every line
                // start a candidate.
                if (not self.multiline) {
                    break;
                }
            }
            start = candidate + 1;
        }
        result.steps = self.matcher.steps;
        return result;
    }

    // Linear time containment test: does the pattern occur at all?  Uses the
    // simulator when it can, because that is where the asymptotic win is.
    fn contains(ref self, subject: string) -> bool {
        if (not self.ok) {
            return false;
        }
        if (self.may_simulate(false)) {
            self.prepare_simulator(subject);
            return self.simulator.search_from(0);
        }
        let found = self.search(subject);
        return found.matched();
    }

    // --- find_all ---------------------------------------------------------
    //
    // Every non-overlapping match, left to right.  An empty match advances
    // the cursor by one so the loop always terminates.

    fn find_all(ref self, subject: string) -> vector(MatchResult) {
        var out = vector(MatchResult)();
        if (not self.ok) {
            return out;
        }
        var cursor: int = 0;
        var guard: int = 0;
        while (cursor <= subject.size()) {
            guard += 1;
            if (guard > subject.size() + 2) {
                break;
            }
            let hit = self.search_from(subject, cursor);
            if (not hit.matched()) {
                break;
            }
            let stop = hit.end();
            let begin = hit.begin();
            out.push_back(hit);
            if (stop == begin) {
                cursor = stop + 1;
            } else {
                cursor = stop;
            }
        }
        return out;
    }

    fn count_matches(ref self, subject: string) -> int {
        let all = self.find_all(subject);
        return all.size();
    }

    // --- replace ----------------------------------------------------------
    //
    // The replacement text may contain `$0` .. `$9`, which stand for the
    // corresponding capture group, and `$$` for a literal dollar.

    fn replace_first(ref self, subject: string, replacement: string) -> string {
        var out = TEXT_EMPTY;
        if (not self.ok) {
            return subject;
        }
        let hit = self.search(subject);
        if (not hit.matched()) {
            return subject;
        }
        out.append(subject.substr(0, hit.begin()));
        out.append(expand_replacement(replacement, hit));
        let tail = hit.end();
        out.append(subject.substr(tail, subject.size() - tail));
        return out;
    }

    fn replace_all(ref self, subject: string, replacement: string) -> string {
        var out = TEXT_EMPTY;
        if (not self.ok) {
            return subject;
        }
        var cursor: int = 0;
        var guard: int = 0;
        while (cursor <= subject.size()) {
            guard += 1;
            if (guard > subject.size() + 2) {
                break;
            }
            let hit = self.search_from(subject, cursor);
            if (not hit.matched()) {
                break;
            }
            let begin = hit.begin();
            let stop = hit.end();
            out.append(subject.substr(cursor, begin - cursor));
            out.append(expand_replacement(replacement, hit));
            if (stop == begin) {
                if (begin < subject.size()) {
                    out.push_back(subject.at(begin));
                }
                cursor = begin + 1;
            } else {
                cursor = stop;
            }
        }
        if (cursor < subject.size()) {
            out.append(subject.substr(cursor, subject.size() - cursor));
        }
        return out;
    }

    // --- split ------------------------------------------------------------
    //
    // Cut the subject at every match.  A limit of zero means "no limit"; any
    // other value caps the number of pieces, with the remainder of the
    // subject forming the last one.

    fn split(ref self, subject: string, limit: int) -> vector(string) {
        var out = vector(string)();
        if (not self.ok) {
            out.push_back(subject);
            return out;
        }
        var cursor: int = 0;
        var guard: int = 0;
        while (cursor <= subject.size()) {
            guard += 1;
            if (guard > subject.size() + 2) {
                break;
            }
            if (limit > 0) {
                if (out.size() + 1 >= limit) {
                    break;
                }
            }
            let hit = self.search_from(subject, cursor);
            if (not hit.matched()) {
                break;
            }
            let begin = hit.begin();
            let stop = hit.end();
            if (stop == begin) {
                // A zero width separator would split between every pair of
                // characters; step over one character instead.
                if (begin >= subject.size()) {
                    break;
                }
                out.push_back(subject.substr(cursor, begin + 1 - cursor));
                cursor = begin + 1;
                continue;
            }
            out.push_back(subject.substr(cursor, begin - cursor));
            cursor = stop;
        }
        out.push_back(subject.substr(cursor, subject.size() - cursor));
        return out;
    }

    // Split and keep only the pieces that are not empty, which is what most
    // tokenising callers actually want.
    fn split_nonempty(ref self, subject: string, limit: int) -> vector(string) {
        let pieces = self.split(subject, limit);
        var out = vector(string)();
        for (piece in pieces) {
            if (not piece.empty()) {
                out.push_back(piece);
            }
        }
        return out;
    }

    // --- Diagnostics ------------------------------------------------------

    fn describe(self) -> string {
        var out = TEXT_EMPTY;
        out.append("pattern ");
        out.append(self.pattern);
        out.append(TEXT_NEWLINE);
        if (not self.ok) {
            out.append("error: ");
            out.append(self.error);
            out.append(" at ");
            out.append(to_string(self.error_position));
            out.append(TEXT_NEWLINE);
            return out;
        }
        out.append("groups: ");
        out.append(to_string(self.parser.group_count));
        out.append(TEXT_NEWLINE);
        out.append("nfa states: ");
        out.append(to_string(self.builder.state_count()));
        out.append(TEXT_NEWLINE);
        out.append("program: ");
        out.append(to_string(self.compiler.program.size()));
        out.append(TEXT_NEWLINE);
        out.append("prefix: ");
        out.append(self.prefix);
        out.append(TEXT_NEWLINE);
        out.append(self.optimiser.stats.describe());
        out.append(TEXT_NEWLINE);
        return out;
    }

    var pattern: string;
    var prefix: string;
    var error: string;
    var parser: PatternParser;
    var builder: NfaBuilder;
    var compiler: Compiler;
    var optimiser: Optimiser;
    var matcher: BacktrackMatcher;
    var simulator: NfaSimulator;
    var engine: EngineChoice = EngineChoice.ChooseAutomatic;
    var error_position: int = 0;
    var ok: bool = false;
    var anchored: bool = false;
    var ignore_case: bool = false;
    var multiline: bool = false;
    var dot_matches_newline: bool = false;
}

// Substitute `$n` references in a replacement template.
fn expand_replacement(template_text: string, hit: MatchResult) -> string {
    var out = TEXT_EMPTY;
    var i: int = 0;
    while (i < template_text.size()) {
        let c = template_text.at(i);
        if (c != CHAR_DOLLAR) {
            out.push_back(c);
            i += 1;
            continue;
        }
        let next = i + 1;
        if (next >= template_text.size()) {
            out.push_back(c);
            i += 1;
            continue;
        }
        let marker = template_text.at(next);
        if (marker == CHAR_DOLLAR) {
            out.push_back(CHAR_DOLLAR);
            i += 2;
            continue;
        }
        if (is_digit(marker)) {
            out.append(hit.group(digit_value(marker)));
            i += 2;
            continue;
        }
        if (marker == CHAR_LBRACE) {
            // `${name}` refers to a named group.
            var scan = next + 1;
            var name = TEXT_EMPTY;
            while (scan < template_text.size()) {
                let n = template_text.at(scan);
                if (n == CHAR_RBRACE) {
                    break;
                }
                name.push_back(n);
                scan += 1;
            }
            var index = NONE;
            for (entry in hit.names) {
                if (entry.name.compare(name) == 0) {
                    index = entry.index;
                    break;
                }
            }
            if (index != NONE) {
                out.append(hit.group(index));
            }
            i = scan + 1;
            continue;
        }
        out.push_back(c);
        i += 1;
    }
    return out;
}

// --- Convenience wrappers ------------------------------------------------
//
// One shot helpers for callers that do not want to keep a compiled pattern
// around.  Each of them pays for compilation, so a loop should build a
// `Regex` once instead.

fn quick_match(pattern: string, subject: string) -> bool {
    var re = Regex();
    if (not re.compile(pattern)) {
        return false;
    }
    let hit = re.match_text(subject);
    return hit.matched();
}

fn quick_search(pattern: string, subject: string) -> bool {
    var re = Regex();
    if (not re.compile(pattern)) {
        return false;
    }
    return re.contains(subject);
}

fn quick_replace(pattern: string, subject: string, replacement: string) -> string {
    var re = Regex();
    if (not re.compile(pattern)) {
        return subject;
    }
    return re.replace_all(subject, replacement);
}

fn quick_split(pattern: string, subject: string) -> vector(string) {
    var re = Regex();
    var out = vector(string)();
    if (not re.compile(pattern)) {
        out.push_back(subject);
        return out;
    }
    return re.split(subject, 0);
}

// Escape every metacharacter in `text` so the result matches it literally.
fn quote_literal(text: string) -> string {
    var out = TEXT_EMPTY;
    var i: int = 0;
    while (i < text.size()) {
        let c = text.at(i);
        if (is_metacharacter(c)) {
            out.push_back(CHAR_BACKSLASH);
        }
        out.push_back(c);
        i += 1;
    }
    return out;
}

// --- Worked examples -----------------------------------------------------
//
// These are not tests - nothing here asserts - but they pin down what the API
// is meant to look like from the outside, and they exercise every entry point
// at least once.

fn example_tokenise_words(subject: string) -> vector(string) {
    var re = Regex();
    if (not re.compile("[a-zA-Z_][a-zA-Z_0-9]*")) {
        return vector(string)();
    }
    var out = vector(string)();
    let hits = re.find_all(subject);
    for (hit in hits) {
        out.push_back(hit.text());
    }
    return out;
}

fn example_strip_comments(subject: string) -> string {
    var re = Regex();
    if (not re.compile("//[^\n]*")) {
        return subject;
    }
    return re.replace_all(subject, TEXT_EMPTY);
}

fn example_parse_date(subject: string) -> vector(int) {
    var out = vector(int)();
    var re = Regex();
    if (not re.compile("(\\d{4})-(\\d{2})-(\\d{2})")) {
        return out;
    }
    let hit = re.match_text(subject);
    if (not hit.matched()) {
        return out;
    }
    out.push_back(parse_int(hit.group(1)));
    out.push_back(parse_int(hit.group(2)));
    out.push_back(parse_int(hit.group(3)));
    return out;
}

fn example_named_groups(subject: string) -> string {
    var re = Regex();
    if (not re.compile("(?<key>\\w+)\\s*=\\s*(?<value>[^;]+);")) {
        return TEXT_EMPTY;
    }
    return re.replace_all(subject, "${value} -> ${key}\n");
}

fn example_split_csv(subject: string) -> vector(string) {
    return quick_split(",\\s*", subject);
}

fn example_repeated_word(subject: string) -> bool {
    var re = Regex();
    // A backreference, so this one has to run on the backtracking matcher.
    if (not re.compile("\\b(\\w+)\\s+\\1\\b")) {
        return false;
    }
    return re.contains(subject);
}

fn example_report(pattern: string) {
    var re = Regex();
    if (not re.compile(pattern)) {
        println(re.error_text());
        return;
    }
    println(re.describe());
    println(re.compiler.program.disassemble());
}

// A rough sanity sweep over the whole engine, written as a function so the
// benchmark corpus contains at least one caller of everything above.
fn self_check() {
    var re = Regex();
    if (not re.compile("^(a|b)+c{2,4}[[:digit:]]*$")) {
        panic(re.error_text());
        return;
    }
    Assert(re.group_count() == 1);
    Assert(re.program_size() > 0);
    Prove(re.state_count() > 0);

    let hit = re.match_text("abbacc42");
    if (hit.matched()) {
        println(hit.text());
    }
    let pieces = quick_split("\\s+", "one two   three");
    for (piece in pieces) {
        println(piece);
    }
    println(quick_replace("[aeiou]", "regular expressions", "*"));
    if (quick_match("\\d+", "12345")) {
        println("all digits");
    }
    if (example_repeated_word("the the end")) {
        println("repeated word found");
    }
    println(quote_literal("1 + 2 * (3 - 4)"));
}

