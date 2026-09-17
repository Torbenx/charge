// A complete JSON reader and writer: lexer, recursive descent parser and
// serializer with compact and pretty output modes.
//
// Numbers are stored as a scaled integer mantissa plus a decimal exponent,
// because the charge lexer only accepts plain decimal integer literals.
// The value of a number is  sign * mantissa * 10^exponent.
namespace json;

// --- Character constants -------------------------------------------------
//
// The lexer compares raw characters constantly, so every delimiter gets a
// name here instead of being spelled out at each use site.

let CHAR_QUOTE: char = '"';
let CHAR_BACKSLASH: char = '\\';
let CHAR_SLASH: char = '/';
let CHAR_NEWLINE: char = '\n';
let CHAR_RETURN: char = '\r';
let CHAR_TAB: char = '\t';
let CHAR_BACKSPACE: char = '\u{08}';
let CHAR_FORMFEED: char = '\u{0C}';
let CHAR_SPACE: char = ' ';
let CHAR_NUL: char = '\0';

let CHAR_LBRACE: char = '{';
let CHAR_RBRACE: char = '}';
let CHAR_LBRACKET: char = '[';
let CHAR_RBRACKET: char = ']';
let CHAR_COLON: char = ':';
let CHAR_COMMA: char = ',';

let CHAR_MINUS: char = '-';
let CHAR_PLUS: char = '+';
let CHAR_DOT: char = '.';
let CHAR_ZERO: char = '0';
let CHAR_NINE: char = '9';
let CHAR_LOWER_A: char = 'a';
let CHAR_LOWER_B: char = 'b';
let CHAR_LOWER_E: char = 'e';
let CHAR_LOWER_F: char = 'f';
let CHAR_LOWER_N: char = 'n';
let CHAR_LOWER_R: char = 'r';
let CHAR_LOWER_T: char = 't';
let CHAR_LOWER_U: char = 'u';
let CHAR_LOWER_Z: char = 'z';
let CHAR_UPPER_A: char = 'A';
let CHAR_UPPER_E: char = 'E';
let CHAR_UPPER_F: char = 'F';
let CHAR_UPPER_Z: char = 'Z';

// --- Keyword and punctuation spellings -----------------------------------

let WORD_TRUE: string = "true";
let WORD_FALSE: string = "false";
let WORD_NULL: string = "null";

let TEXT_LBRACE: string = "{";
let TEXT_RBRACE: string = "}";
let TEXT_LBRACKET: string = "[";
let TEXT_RBRACKET: string = "]";
let TEXT_COLON: string = ":";
let TEXT_COMMA: string = ",";
let TEXT_COLON_SPACE: string = ": ";
let TEXT_COMMA_SPACE: string = ", ";
let TEXT_EMPTY: string = "";
let TEXT_EMPTY_ARRAY: string = "[]";
let TEXT_EMPTY_OBJECT: string = "{}";
let TEXT_SPACE: string = " ";
let TEXT_NEWLINE: string = "\n";
let TEXT_TWO_SPACES: string = "  ";
let TEXT_FOUR_SPACES: string = "    ";
let TEXT_TAB: string = "\t";

// The literal two-character sequences that JSON uses inside strings. Note the
// doubled backslash: the charge source escape produces one real backslash.
let ESCAPE_QUOTE: string = "\\\"";
let ESCAPE_BACKSLASH: string = "\\\\";
let ESCAPE_SLASH: string = "\\/";
let ESCAPE_NEWLINE: string = "\\n";
let ESCAPE_RETURN: string = "\\r";
let ESCAPE_TAB: string = "\\t";
let ESCAPE_BACKSPACE: string = "\\b";
let ESCAPE_FORMFEED: string = "\\f";
let ESCAPE_UNICODE: string = "\\u";

// Limits. JSON itself has none, but a real reader needs guard rails.
let MAX_DEPTH: int = 128;
let MAX_MANTISSA_DIGITS: int = 17;
let MAX_EXPONENT: int = 308;
let MIN_EXPONENT: int = -308;
let UNICODE_ESCAPE_DIGITS: int = 4;
let SURROGATE_HIGH_BEGIN: int = 55296;
let SURROGATE_HIGH_END: int = 56319;
let SURROGATE_LOW_BEGIN: int = 56320;
let SURROGATE_LOW_END: int = 57343;
let SURROGATE_OFFSET: int = 65536;
let REPLACEMENT_CODEPOINT: int = 65533;
let ASCII_LIMIT: int = 128;
let CONTROL_LIMIT: int = 32;

// --- Value and token kinds -----------------------------------------------

choice JsonKind {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
}

choice TokenKind {
    End,
    ObjectBegin,
    ObjectEnd,
    ArrayBegin,
    ArrayEnd,
    Colon,
    Comma,
    String,
    Number,
    True,
    False,
    Null,
    Invalid,
}

choice ErrorCode {
    None,
    UnexpectedEnd,
    UnexpectedCharacter,
    UnterminatedString,
    BadEscape,
    BadUnicodeEscape,
    ControlCharacterInString,
    LeadingZero,
    MissingFraction,
    MissingExponent,
    NumberOverflow,
    TrailingContent,
    ExpectedColon,
    ExpectedCommaOrEnd,
    ExpectedKey,
    ExpectedValue,
    DepthExceeded,
    LoneSurrogate,
}

// --- Source positions ----------------------------------------------------

// Every token and every error carries one of these so diagnostics can point
// at a line and column rather than a bare byte offset.
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
        out.append(" (offset ");
        out.append(to_string(self.offset));
        out.append(")");
        return out;
    }

    var offset: int = 0;
    var line: int = 1;
    var column: int = 1;
}

// --- Escape tables -------------------------------------------------------

// One row of the escape table: the character that follows a backslash, the
// character it decodes to, and the human readable name used in diagnostics.
class EscapeRow {
    var marker: char = CHAR_NUL;
    var decoded: char = CHAR_NUL;
    var spelling: string;
    var name: string;
}

fn escape_table() -> vector(EscapeRow) {
    var rows: vector(EscapeRow);
    rows.push_back(EscapeRow({.marker = '"', .decoded = '"', .spelling = "\\\"", .name = "quotation mark"}));
    rows.push_back(EscapeRow({.marker = '\\', .decoded = '\\', .spelling = "\\\\", .name = "reverse solidus"}));
    rows.push_back(EscapeRow({.marker = '/', .decoded = '/', .spelling = "\\/", .name = "solidus"}));
    rows.push_back(EscapeRow({.marker = 'b', .decoded = '\u{08}', .spelling = "\\b", .name = "backspace"}));
    rows.push_back(EscapeRow({.marker = 'f', .decoded = '\u{0C}', .spelling = "\\f", .name = "form feed"}));
    rows.push_back(EscapeRow({.marker = 'n', .decoded = '\n', .spelling = "\\n", .name = "line feed"}));
    rows.push_back(EscapeRow({.marker = 'r', .decoded = '\r', .spelling = "\\r", .name = "carriage return"}));
    rows.push_back(EscapeRow({.marker = 't', .decoded = '\t', .spelling = "\\t", .name = "tab"}));
    return rows;
}

// Decode a single escape marker. Returns the NUL character when the marker is
// not one of the eight single-character escapes; \u is handled separately.
fn decode_escape(marker: char) -> char {
    if (marker == '"') {
        return '"';
    }
    if (marker == '\\') {
        return '\\';
    }
    if (marker == '/') {
        return '/';
    }
    if (marker == 'b') {
        return '\u{08}';
    }
    if (marker == 'f') {
        return '\u{0C}';
    }
    if (marker == 'n') {
        return '\n';
    }
    if (marker == 'r') {
        return '\r';
    }
    if (marker == 't') {
        return '\t';
    }
    return CHAR_NUL;
}

// The inverse mapping, used by the serializer.
fn encode_escape(c: char) -> string {
    if (c == '"') {
        return "\\\"";
    }
    if (c == '\\') {
        return "\\\\";
    }
    if (c == '\n') {
        return "\\n";
    }
    if (c == '\r') {
        return "\\r";
    }
    if (c == '\t') {
        return "\\t";
    }
    if (c == '\u{08}') {
        return "\\b";
    }
    if (c == '\u{0C}') {
        return "\\f";
    }
    return "";
}

fn escape_name(marker: char) -> string {
    if (marker == 'n') {
        return "line feed";
    }
    if (marker == 't') {
        return "tab";
    }
    if (marker == 'r') {
        return "carriage return";
    }
    if (marker == 'b') {
        return "backspace";
    }
    if (marker == 'f') {
        return "form feed";
    }
    if (marker == 'u') {
        return "unicode escape";
    }
    if (marker == '/') {
        return "solidus";
    }
    if (marker == '"') {
        return "quotation mark";
    }
    if (marker == '\\') {
        return "reverse solidus";
    }
    return "unknown escape";
}

// --- Character classification --------------------------------------------

fn is_whitespace(c: char) => c == ' ' or c == '\t' or c == '\n' or c == '\r';

fn is_digit(c: char) => c >= '0' and c <= '9';

fn is_nonzero_digit(c: char) => c >= '1' and c <= '9';

fn is_hex_digit(c: char) -> bool {
    if (c >= '0' and c <= '9') {
        return true;
    }
    if (c >= 'a' and c <= 'f') {
        return true;
    }
    if (c >= 'A' and c <= 'F') {
        return true;
    }
    return false;
}

fn is_structural(c: char) -> bool {
    return c == '{' or c == '}' or c == '[' or c == ']' or c == ':' or c == ',';
}

fn is_control(c: char) -> bool {
    return int(c) < CONTROL_LIMIT;
}

fn is_letter(c: char) -> bool {
    if (c >= 'a' and c <= 'z') {
        return true;
    }
    if (c >= 'A' and c <= 'Z') {
        return true;
    }
    return false;
}

fn digit_value(c: char) -> int {
    return int(c) - int('0');
}

fn hex_value(c: char) -> int {
    if (c >= '0' and c <= '9') {
        return int(c) - int('0');
    }
    if (c >= 'a' and c <= 'f') {
        return int(c) - int('a') + 10;
    }
    if (c >= 'A' and c <= 'F') {
        return int(c) - int('A') + 10;
    }
    return -1;
}

// Lower case hexadecimal digits, indexed by value. Used when the serializer
// has to emit a \u escape for a control character.
fn hex_digit(value: int) -> char {
    if (value == 0) { return '0'; }
    if (value == 1) { return '1'; }
    if (value == 2) { return '2'; }
    if (value == 3) { return '3'; }
    if (value == 4) { return '4'; }
    if (value == 5) { return '5'; }
    if (value == 6) { return '6'; }
    if (value == 7) { return '7'; }
    if (value == 8) { return '8'; }
    if (value == 9) { return '9'; }
    if (value == 10) { return 'a'; }
    if (value == 11) { return 'b'; }
    if (value == 12) { return 'c'; }
    if (value == 13) { return 'd'; }
    if (value == 14) { return 'e'; }
    if (value == 15) { return 'f'; }
    return '?';
}

// --- Diagnostics ---------------------------------------------------------

fn error_text(code: ErrorCode) -> string {
    if (code == ErrorCode.None) {
        return "no error";
    }
    if (code == ErrorCode.UnexpectedEnd) {
        return "unexpected end of input";
    }
    if (code == ErrorCode.UnexpectedCharacter) {
        return "unexpected character";
    }
    if (code == ErrorCode.UnterminatedString) {
        return "unterminated string literal, expected a closing '\"'";
    }
    if (code == ErrorCode.BadEscape) {
        return "invalid escape sequence after '\\'";
    }
    if (code == ErrorCode.BadUnicodeEscape) {
        return "invalid \\uXXXX escape, expected four hexadecimal digits";
    }
    if (code == ErrorCode.ControlCharacterInString) {
        return "raw control character in string, use \\u00XX instead";
    }
    if (code == ErrorCode.LeadingZero) {
        return "numbers may not have a leading zero";
    }
    if (code == ErrorCode.MissingFraction) {
        return "expected at least one digit after '.'";
    }
    if (code == ErrorCode.MissingExponent) {
        return "expected at least one digit after 'e'";
    }
    if (code == ErrorCode.NumberOverflow) {
        return "number is out of the representable range";
    }
    if (code == ErrorCode.TrailingContent) {
        return "trailing content after the top level value";
    }
    if (code == ErrorCode.ExpectedColon) {
        return "expected ':' between an object key and its value";
    }
    if (code == ErrorCode.ExpectedCommaOrEnd) {
        return "expected ',' or a closing bracket";
    }
    if (code == ErrorCode.ExpectedKey) {
        return "expected a string key";
    }
    if (code == ErrorCode.ExpectedValue) {
        return "expected a value";
    }
    if (code == ErrorCode.DepthExceeded) {
        return "maximum nesting depth exceeded";
    }
    if (code == ErrorCode.LoneSurrogate) {
        return "lone surrogate in \\u escape";
    }
    return "unknown error";
}

class Diagnostic {

    fn valid(self) -> bool {
        return self.code == ErrorCode.None;
    }

    fn render(self) -> string {
        if (self.code == ErrorCode.None) {
            return "ok";
        }
        var out: string = "json: ";
        out.append(error_text(self.code));
        out.append(" at ");
        out.append(self.where_.describe());
        if (not self.detail.empty()) {
            out.append(" [");
            out.append(self.detail);
            out.append("]");
        }
        return out;
    }

    var code: ErrorCode = ErrorCode.None;
    var where_: Position;
    var detail: string;
}

fn make_error(code: ErrorCode, where_: Position, detail: string) -> Diagnostic {
    return Diagnostic({.code = code, .where_ = where_, .detail = detail});
}

// --- Numbers -------------------------------------------------------------

// A JSON number as sign * mantissa * 10^exponent. Storing the mantissa as an
// integer keeps the whole reader free of floating point.
class Number {

    fn is_integral(self) -> bool {
        return self.exponent >= 0;
    }

    fn signed_mantissa(self) -> int {
        if (self.negative) {
            return -self.mantissa;
        }
        return self.mantissa;
    }

    fn render(self) -> string {
        var out: string = "";
        if (self.negative) {
            out.append("-");
        }
        out.append(to_string(self.mantissa));
        if (self.exponent != 0) {
            out.append("e");
            out.append(to_string(self.exponent));
        }
        return out;
    }

    var negative: bool = false;
    var mantissa: int = 0;
    var exponent: int = 0;
    var digits: int = 0;
}

fn number_from_int(value: int) -> Number {
    if (value < 0) {
        return Number({.negative = true, .mantissa = -value, .exponent = 0});
    }
    return Number({.negative = false, .mantissa = value, .exponent = 0});
}

// --- Tokens --------------------------------------------------------------

class Token {

    fn is_value_start(self) -> bool {
        if (self.kind == TokenKind.String) { return true; }
        if (self.kind == TokenKind.Number) { return true; }
        if (self.kind == TokenKind.True) { return true; }
        if (self.kind == TokenKind.False) { return true; }
        if (self.kind == TokenKind.Null) { return true; }
        if (self.kind == TokenKind.ArrayBegin) { return true; }
        if (self.kind == TokenKind.ObjectBegin) { return true; }
        return false;
    }

    fn spelling(self) -> string {
        if (self.kind == TokenKind.End) { return "<end of input>"; }
        if (self.kind == TokenKind.ObjectBegin) { return "{"; }
        if (self.kind == TokenKind.ObjectEnd) { return "}"; }
        if (self.kind == TokenKind.ArrayBegin) { return "["; }
        if (self.kind == TokenKind.ArrayEnd) { return "]"; }
        if (self.kind == TokenKind.Colon) { return ":"; }
        if (self.kind == TokenKind.Comma) { return ","; }
        if (self.kind == TokenKind.True) { return "true"; }
        if (self.kind == TokenKind.False) { return "false"; }
        if (self.kind == TokenKind.Null) { return "null"; }
        if (self.kind == TokenKind.String) { return self.text; }
        if (self.kind == TokenKind.Number) { return self.number.render(); }
        return "<invalid>";
    }

    var kind: TokenKind = TokenKind.End;
    var text: string;
    var number: Number;
    var begin: Position;
    var end: Position;
}

fn punctuation_token(c: char) -> TokenKind {
    if (c == '{') {
        return TokenKind.ObjectBegin;
    }
    if (c == '}') {
        return TokenKind.ObjectEnd;
    }
    if (c == '[') {
        return TokenKind.ArrayBegin;
    }
    if (c == ']') {
        return TokenKind.ArrayEnd;
    }
    if (c == ':') {
        return TokenKind.Colon;
    }
    if (c == ',') {
        return TokenKind.Comma;
    }
    return TokenKind.Invalid;
}

// --- Lexer ---------------------------------------------------------------

// A character level scanner. It owns the source text and a cursor, and hands
// out one token at a time. All string decoding happens here so the parser
// never sees a backslash.
class Lexer {

    fn at_end(self) -> bool {
        return self.cursor.offset >= self.source.size();
    }

    fn remaining(self) -> int {
        return self.source.size() - self.cursor.offset;
    }

    fn peek(self) -> char {
        if (self.at_end()) {
            return '\0';
        }
        return self.source.at(self.cursor.offset);
    }

    fn peek_at(self, ahead: int) -> char {
        let index = self.cursor.offset + ahead;
        if (index >= self.source.size()) {
            return '\0';
        }
        return self.source.at(index);
    }

    fn advance(ref self) -> char {
        if (self.at_end()) {
            return '\0';
        }
        let c = self.source.at(self.cursor.offset);
        self.cursor.advance_over(c);
        return c;
    }

    fn accept(ref self, expected: char) -> bool {
        if (self.peek() == expected) {
            Discard(self.advance());
            return true;
        }
        return false;
    }

    fn fail(ref self, code: ErrorCode, detail: string) -> Token {
        self.problem = make_error(code, self.cursor, detail);
        return Token({.kind = TokenKind.Invalid, .begin = self.cursor, .end = self.cursor});
    }

    // Skip spaces, tabs, carriage returns and newlines. JSON allows nothing
    // else between tokens, comments in particular are not part of the grammar.
    fn skip_whitespace(ref self) {
        while (not self.at_end()) {
            let c = self.peek();
            if (c == ' ' or c == '\t' or c == '\n' or c == '\r') {
                Discard(self.advance());
            } else {
                break;
            }
        }
    }

    // A relaxed mode that also swallows // and /* */ comments, for reading
    // configuration files that are "JSON with comments".
    fn skip_trivia(ref self) {
        while (true) {
            self.skip_whitespace();
            if (not self.allow_comments) {
                break;
            }
            if (self.peek() != '/') {
                break;
            }
            let after = self.peek_at(1);
            if (after == '/') {
                Discard(self.advance());
                Discard(self.advance());
                while (not self.at_end() and self.peek() != '\n') {
                    Discard(self.advance());
                }
            } else {
                if (after == '*') {
                    Discard(self.advance());
                    Discard(self.advance());
                    while (not self.at_end()) {
                        if (self.peek() == '*' and self.peek_at(1) == '/') {
                            Discard(self.advance());
                            Discard(self.advance());
                            break;
                        }
                        Discard(self.advance());
                    }
                } else {
                    break;
                }
            }
        }
    }

    // Read four hexadecimal digits and fold them into a code point. The
    // leading "\u" has already been consumed by the caller.
    fn scan_hex_quad(ref self, ref out: int) -> bool {
        var value: int = 0;
        var read: int = 0;
        while (read < UNICODE_ESCAPE_DIGITS) {
            if (self.at_end()) {
                return false;
            }
            let c = self.peek();
            if (not is_hex_digit(c)) {
                return false;
            }
            Discard(self.advance());
            value = value * 16 + hex_value(c);
            read += 1;
        }
        Assert(read == UNICODE_ESCAPE_DIGITS);
        out = value;
        return true;
    }

    // Encode a code point as UTF-8 and append it to the destination. The
    // charge string is a byte sequence, so this writes one to four bytes.
    fn append_utf8(self, ref out: string, codepoint: int) {
        if (codepoint < ASCII_LIMIT) {
            out.push_back(char(codepoint));
            return;
        }
        if (codepoint < 2048) {
            out.push_back(char(192 + (codepoint >> 6)));
            out.push_back(char(128 + (codepoint & 63)));
            return;
        }
        if (codepoint < 65536) {
            out.push_back(char(224 + (codepoint >> 12)));
            out.push_back(char(128 + ((codepoint >> 6) & 63)));
            out.push_back(char(128 + (codepoint & 63)));
            return;
        }
        out.push_back(char(240 + (codepoint >> 18)));
        out.push_back(char(128 + ((codepoint >> 12) & 63)));
        out.push_back(char(128 + ((codepoint >> 6) & 63)));
        out.push_back(char(128 + (codepoint & 63)));
    }

    // Handle everything after a backslash inside a string literal.
    fn scan_escape(ref self, ref out: string) -> bool {
        if (self.at_end()) {
            Discard(self.fail(ErrorCode.UnterminatedString, "input ended after a backslash"));
            return false;
        }
        let marker = self.advance();
        if (marker == 'u') {
            var codepoint: int = 0;
            if (not self.scan_hex_quad(codepoint)) {
                Discard(self.fail(ErrorCode.BadUnicodeEscape, "expected four hex digits after \\u"));
                return false;
            }
            // A high surrogate must be followed by a matching low surrogate,
            // spelled as a second \u escape.
            if (codepoint >= SURROGATE_HIGH_BEGIN and codepoint <= SURROGATE_HIGH_END) {
                if (self.peek() != '\\' or self.peek_at(1) != 'u') {
                    if (self.strict_surrogates) {
                        Discard(self.fail(ErrorCode.LoneSurrogate, "high surrogate without a pair"));
                        return false;
                    }
                    self.append_utf8(out, REPLACEMENT_CODEPOINT);
                    return true;
                }
                Discard(self.advance());
                Discard(self.advance());
                var low: int = 0;
                if (not self.scan_hex_quad(low)) {
                    Discard(self.fail(ErrorCode.BadUnicodeEscape, "bad low surrogate escape"));
                    return false;
                }
                if (low < SURROGATE_LOW_BEGIN or low > SURROGATE_LOW_END) {
                    if (self.strict_surrogates) {
                        Discard(self.fail(ErrorCode.LoneSurrogate, "second escape is not a low surrogate"));
                        return false;
                    }
                    self.append_utf8(out, REPLACEMENT_CODEPOINT);
                    return true;
                }
                let high_part = (codepoint - SURROGATE_HIGH_BEGIN) << 10;
                let low_part = low - SURROGATE_LOW_BEGIN;
                self.append_utf8(out, SURROGATE_OFFSET + high_part + low_part);
                return true;
            }
            if (codepoint >= SURROGATE_LOW_BEGIN and codepoint <= SURROGATE_LOW_END) {
                if (self.strict_surrogates) {
                    Discard(self.fail(ErrorCode.LoneSurrogate, "unpaired low surrogate"));
                    return false;
                }
                self.append_utf8(out, REPLACEMENT_CODEPOINT);
                return true;
            }
            self.append_utf8(out, codepoint);
            return true;
        }
        let decoded = decode_escape(marker);
        if (decoded == '\0') {
            var detail: string = "unknown escape '\\";
            detail.push_back(marker);
            detail.append("'");
            Discard(self.fail(ErrorCode.BadEscape, detail));
            return false;
        }
        out.push_back(decoded);
        return true;
    }

    // Scan a complete string literal. The opening quote is consumed by the
    // caller; the closing quote is consumed here.
    fn scan_string(ref self) -> Token {
        let begin = self.cursor;
        var text: string = "";
        while (true) {
            if (self.at_end()) {
                return self.fail(ErrorCode.UnterminatedString, "reached end of input inside a string");
            }
            let c = self.peek();
            if (c == '"') {
                Discard(self.advance());
                break;
            }
            if (c == '\\') {
                Discard(self.advance());
                if (not self.scan_escape(text)) {
                    return Token({.kind = TokenKind.Invalid, .begin = begin, .end = self.cursor});
                }
                continue;
            }
            if (is_control(c)) {
                var detail: string = "control byte ";
                detail.append(to_string(int(c)));
                return self.fail(ErrorCode.ControlCharacterInString, detail);
            }
            Discard(self.advance());
            text.push_back(c);
        }
        return Token({.kind = TokenKind.String, .text = text, .begin = begin, .end = self.cursor});
    }

    // Scan an integer, fraction and exponent into a scaled mantissa.
    fn scan_number(ref self) -> Token {
        let begin = self.cursor;
        var result: Number;
        if (self.peek() == '-') {
            Discard(self.advance());
            result.negative = true;
        }
        if (self.at_end()) {
            return self.fail(ErrorCode.UnexpectedEnd, "input ended after '-'");
        }
        var seen: int = 0;
        if (self.peek() == '0') {
            Discard(self.advance());
            seen += 1;
            if (is_digit(self.peek())) {
                return self.fail(ErrorCode.LeadingZero, "digit follows a leading '0'");
            }
        } else {
            if (not is_digit(self.peek())) {
                return self.fail(ErrorCode.UnexpectedCharacter, "expected a digit");
            }
            while (is_digit(self.peek())) {
                let c = self.advance();
                if (seen < MAX_MANTISSA_DIGITS) {
                    result.mantissa = result.mantissa * 10 + digit_value(c);
                } else {
                    // Past the precision limit the digit only shifts the
                    // decimal point, it no longer changes the mantissa.
                    result.exponent += 1;
                }
                seen += 1;
            }
        }
        result.digits = seen;
        // The fractional part scales the mantissa up and the exponent down.
        if (self.peek() == '.') {
            Discard(self.advance());
            if (not is_digit(self.peek())) {
                return self.fail(ErrorCode.MissingFraction, "expected a digit after '.'");
            }
            while (is_digit(self.peek())) {
                let c = self.advance();
                if (seen < MAX_MANTISSA_DIGITS) {
                    result.mantissa = result.mantissa * 10 + digit_value(c);
                    result.exponent -= 1;
                    seen += 1;
                }
            }
            result.digits = seen;
        }
        if (self.peek() == 'e' or self.peek() == 'E') {
            Discard(self.advance());
            var exponent_negative: bool = false;
            if (self.peek() == '+') {
                Discard(self.advance());
            } else {
                if (self.peek() == '-') {
                    Discard(self.advance());
                    exponent_negative = true;
                }
            }
            if (not is_digit(self.peek())) {
                return self.fail(ErrorCode.MissingExponent, "expected a digit after the exponent marker");
            }
            var exponent: int = 0;
            while (is_digit(self.peek())) {
                let c = self.advance();
                exponent = exponent * 10 + digit_value(c);
                if (exponent > MAX_EXPONENT) {
                    return self.fail(ErrorCode.NumberOverflow, "exponent is too large");
                }
            }
            if (exponent_negative) {
                result.exponent -= exponent;
            } else {
                result.exponent += exponent;
            }
        }
        if (result.exponent > MAX_EXPONENT or result.exponent < MIN_EXPONENT) {
            return self.fail(ErrorCode.NumberOverflow, "scaled exponent is out of range");
        }
        return Token({.kind = TokenKind.Number, .number = result, .begin = begin, .end = self.cursor});
    }

    // Match one of the three bare keywords.
    fn scan_keyword(ref self) -> Token {
        let begin = self.cursor;
        var word: string = "";
        while (is_letter(self.peek())) {
            word.push_back(self.advance());
        }
        if (word == "true") {
            return Token({.kind = TokenKind.True, .text = "true", .begin = begin, .end = self.cursor});
        }
        if (word == "false") {
            return Token({.kind = TokenKind.False, .text = "false", .begin = begin, .end = self.cursor});
        }
        if (word == "null") {
            return Token({.kind = TokenKind.Null, .text = "null", .begin = begin, .end = self.cursor});
        }
        var detail: string = "expected 'true', 'false' or 'null', found '";
        detail.append(word);
        detail.append("'");
        return self.fail(ErrorCode.UnexpectedCharacter, detail);
    }

    fn next_token(ref self) -> Token {
        self.skip_trivia();
        let begin = self.cursor;
        if (self.at_end()) {
            return Token({.kind = TokenKind.End, .begin = begin, .end = begin});
        }
        let c = self.peek();
        if (c == '"') {
            Discard(self.advance());
            return self.scan_string();
        }
        if (c == '-' or is_digit(c)) {
            return self.scan_number();
        }
        if (is_letter(c)) {
            return self.scan_keyword();
        }
        if (is_structural(c)) {
            Discard(self.advance());
            return Token({.kind = punctuation_token(c), .text = self.source.substr(begin.offset, 1), .begin = begin, .end = self.cursor});
        }
        var detail: string = "stray byte ";
        detail.append(to_string(int(c)));
        Discard(self.advance());
        return self.fail(ErrorCode.UnexpectedCharacter, detail);
    }

    var source: string;
    var cursor: Position;
    var problem: Diagnostic;
    var allow_comments: bool = false;
    var strict_surrogates: bool = true;
}


// --- Values --------------------------------------------------------------

// A JSON value is a tagged union. charge has no variant type yet, so every
// payload gets its own field and `kind` decides which one is meaningful.
//
// Objects keep two parallel vectors instead of a map because JSON member
// order is observable: a document that is read and written back out should
// come out spelled the same way it went in.
class JsonValue {

    fn is_null(self) -> bool {
        return self.kind == JsonKind.Null;
    }

    fn is_bool(self) -> bool {
        return self.kind == JsonKind.Bool;
    }

    fn is_number(self) -> bool {
        return self.kind == JsonKind.Number;
    }

    fn is_string(self) -> bool {
        return self.kind == JsonKind.String;
    }

    fn is_array(self) -> bool {
        return self.kind == JsonKind.Array;
    }

    fn is_object(self) -> bool {
        return self.kind == JsonKind.Object;
    }

    // True for everything that cannot contain another value.
    fn is_scalar(self) -> bool {
        if (self.kind == JsonKind.Array) {
            return false;
        }
        if (self.kind == JsonKind.Object) {
            return false;
        }
        return true;
    }

    fn kind_name(self) -> string {
        if (self.kind == JsonKind.Null) { return "null"; }
        if (self.kind == JsonKind.Bool) { return "bool"; }
        if (self.kind == JsonKind.Number) { return "number"; }
        if (self.kind == JsonKind.String) { return "string"; }
        if (self.kind == JsonKind.Array) { return "array"; }
        if (self.kind == JsonKind.Object) { return "object"; }
        return "<unknown>";
    }

    // The number of children. Scalars report zero rather than failing, which
    // makes the tree walkers below a lot shorter.
    fn size(self) -> int {
        if (self.kind == JsonKind.Array) {
            return self.items.size();
        }
        if (self.kind == JsonKind.Object) {
            return self.keys.size();
        }
        return 0;
    }

    fn empty(self) -> bool {
        return self.size() == 0;
    }

    // --- Array access ----------------------------------------------------

    fn at(ref self, index: int) -> ref JsonValue {
        Assert(index >= 0);
        if (self.kind != JsonKind.Array) {
            return self.absent;
        }
        if (index >= self.items.size()) {
            return self.absent;
        }
        return self.items[index];
    }

    fn first(ref self) -> ref JsonValue {
        return self.at(0);
    }

    fn last(ref self) -> ref JsonValue {
        return self.at(self.size() - 1);
    }

    fn push_back(ref self, value: JsonValue) {
        self.kind = JsonKind.Array;
        self.items.push_back(value);
    }

    // --- Object access ---------------------------------------------------

    fn index_of(self, name: string) -> int {
        if (self.kind != JsonKind.Object) {
            return -1;
        }
        var slot: int = 0;
        while (slot < self.keys.size()) {
            if (self.keys[slot] == *name) {
                return slot;
            }
            slot += 1;
        }
        return -1;
    }

    fn has_member(self, name: string) -> bool {
        return self.index_of(name) >= 0;
    }

    fn member(ref self, name: string) -> ref JsonValue {
        let slot = self.index_of(name);
        if (slot < 0) {
            return self.absent;
        }
        return self.values[slot];
    }

    fn key_at(self, slot: int) -> string {
        if (slot < 0 or slot >= self.keys.size()) {
            return "";
        }
        return self.keys[slot];
    }

    fn value_at(ref self, slot: int) -> ref JsonValue {
        if (slot < 0 or slot >= self.values.size()) {
            return self.absent;
        }
        return self.values[slot];
    }

    // Insert or overwrite. Returns true when the key was new.
    fn set_member(ref self, name: string, value: JsonValue) -> bool {
        self.kind = JsonKind.Object;
        let slot = self.index_of(name);
        if (slot >= 0) {
            self.values[slot] = value;
            return false;
        }
        self.keys.push_back(name);
        self.values.push_back(value);
        return true;
    }

    fn remove_member(ref self, name: string) -> bool {
        let slot = self.index_of(name);
        if (slot < 0) {
            return false;
        }
        self.keys.erase(slot);
        self.values.erase(slot);
        return true;
    }

    // --- Scalar readers --------------------------------------------------

    fn as_bool(self, fallback: bool) -> bool {
        if (self.kind != JsonKind.Bool) {
            return fallback;
        }
        return self.boolean;
    }

    fn as_int(self, fallback: int) -> int {
        if (self.kind != JsonKind.Number) {
            return fallback;
        }
        if (self.number.exponent < 0) {
            return fallback;
        }
        var scaled = self.number.signed_mantissa();
        var steps = self.number.exponent;
        while (steps > 0) {
            scaled = scaled * 10;
            steps -= 1;
        }
        return scaled;
    }

    fn as_string(self, fallback: string) -> string {
        if (self.kind != JsonKind.String) {
            return *fallback;
        }
        return self.text;
    }

    // A short one line description used in error messages and in tests.
    fn describe(self) -> string {
        var out: string = "<";
        out.append(self.kind_name());
        if (self.kind == JsonKind.String) {
            out.append(" \"");
            out.append(self.text);
            out.append("\"");
        }
        if (self.kind == JsonKind.Number) {
            out.append(" ");
            out.append(self.number.render());
        }
        if (self.kind == JsonKind.Bool) {
            if (self.boolean) {
                out.append(" true");
            } else {
                out.append(" false");
            }
        }
        if (self.kind == JsonKind.Array or self.kind == JsonKind.Object) {
            out.append(" of ");
            out.append(to_string(self.size()));
        }
        out.append(">");
        return out;
    }

    fn clear(ref self) {
        self.kind = JsonKind.Null;
        self.boolean = false;
        self.text.clear();
        self.items.clear();
        self.keys.clear();
        self.values.clear();
    }

    var kind: JsonKind = JsonKind.Null;
    var boolean: bool = false;
    var number: Number;
    var text: string;
    var items: vector(JsonValue);
    var keys: vector(string);
    var values: vector(JsonValue);
    // Returned by at() and member() when the lookup misses, so callers can
    // chain accessors without checking every step.
    var absent: vector(JsonValue);
}

// --- Value constructors --------------------------------------------------

fn make_null() -> JsonValue {
    return JsonValue({.kind = JsonKind.Null});
}

fn make_bool(value: bool) -> JsonValue {
    return JsonValue({.kind = JsonKind.Bool, .boolean = value});
}

fn make_number(value: Number) -> JsonValue {
    return JsonValue({.kind = JsonKind.Number, .number = value});
}

fn make_int(value: int) -> JsonValue {
    return JsonValue({.kind = JsonKind.Number, .number = number_from_int(value)});
}

// Build a scaled number: `mantissa * 10^exponent`, e.g. make_scaled(1505, -2)
// is the JSON number 15.05.
fn make_scaled(mantissa: int, exponent: int) -> JsonValue {
    var n = number_from_int(mantissa);
    n.exponent = exponent;
    return JsonValue({.kind = JsonKind.Number, .number = n});
}

fn make_string(value: string) -> JsonValue {
    return JsonValue({.kind = JsonKind.String, .text = value});
}

fn make_array() -> JsonValue {
    return JsonValue({.kind = JsonKind.Array});
}

fn make_object() -> JsonValue {
    return JsonValue({.kind = JsonKind.Object});
}

// --- Parser --------------------------------------------------------------

// How a document is allowed to end. Strict JSON wants exactly one value and
// nothing after it; the streaming mode leaves the cursor where it stopped so
// a caller can read newline delimited JSON.
choice DocumentMode {
    SingleValue,
    Streaming,
}

// Recursive descent over the token stream produced by Lexer. The parser
// keeps one token of lookahead in `current`; every rule is written so that
// on entry `current` is the first token of the construct and on exit it is
// the first token after it.
class Parser {

    fn ok(self) -> bool {
        return self.problem.code == ErrorCode.None;
    }

    fn diagnostic(self) -> Diagnostic {
        return self.problem;
    }

    fn position(self) -> Position {
        return self.current.begin;
    }

    // Pull the next token, forwarding any lexer level error. Once the parser
    // is in an error state it keeps returning the end token so the recursion
    // unwinds instead of spinning.
    fn advance(ref self) {
        if (not self.ok()) {
            self.current = Token({.kind = TokenKind.End, .begin = self.current.end, .end = self.current.end});
            return;
        }
        self.current = self.lexer.next_token();
        self.tokens_read += 1;
        if (self.current.kind == TokenKind.Invalid) {
            self.problem = self.lexer.problem;
            if (self.problem.code == ErrorCode.None) {
                self.problem = make_error(ErrorCode.UnexpectedCharacter, self.current.begin, "invalid token");
            }
        }
    }

    fn check(self, kind: TokenKind) -> bool {
        return self.current.kind == kind;
    }

    fn at_end(self) -> bool {
        return self.current.kind == TokenKind.End;
    }

    // Consume `kind` if it is next, otherwise raise `code`.
    fn expect(ref self, kind: TokenKind, code: ErrorCode, detail: string) -> bool {
        if (self.current.kind != kind) {
            self.raise(code, detail);
            return false;
        }
        self.advance();
        return true;
    }

    // Record the first error only. Later errors are almost always fallout
    // from the first one and just make the report noisier.
    fn raise(ref self, code: ErrorCode, detail: string) {
        if (not self.ok()) {
            return;
        }
        var text: string = detail;
        text.append(", found ");
        text.append(self.quoted_spelling());
        self.problem = make_error(code, self.current.begin, text);
    }

    fn quoted_spelling(self) -> string {
        if (self.current.kind == TokenKind.End) {
            return "end of input";
        }
        var out: string = "'";
        out.append(self.current.spelling());
        out.append("'");
        return out;
    }

    // --- Grammar rules ---------------------------------------------------

    // value := object | array | string | number | 'true' | 'false' | 'null'
    fn parse_value(ref self, depth: int) -> JsonValue {
        if (depth > self.depth_limit) {
            var detail: string = "nesting is deeper than ";
            detail.append(to_string(self.depth_limit));
            detail.append(" levels");
            self.problem = make_error(ErrorCode.DepthExceeded, self.current.begin, detail);
            return make_null();
        }
        if (depth > self.deepest) {
            self.deepest = depth;
        }
        if (self.current.kind == TokenKind.ObjectBegin) {
            return self.parse_object(depth);
        }
        if (self.current.kind == TokenKind.ArrayBegin) {
            return self.parse_array(depth);
        }
        if (self.current.kind == TokenKind.String) {
            let text = self.current.text;
            self.advance();
            return make_string(text);
        }
        if (self.current.kind == TokenKind.Number) {
            let value = self.current.number;
            self.advance();
            return make_number(value);
        }
        if (self.current.kind == TokenKind.True) {
            self.advance();
            return make_bool(true);
        }
        if (self.current.kind == TokenKind.False) {
            self.advance();
            return make_bool(false);
        }
        if (self.current.kind == TokenKind.Null) {
            self.advance();
            return make_null();
        }
        self.raise(ErrorCode.ExpectedValue, "expected 'null', 'true', 'false', a number, a string, '[' or '{'");
        return make_null();
    }

    // array := '[' ']' | '[' value { ',' value } ']'
    fn parse_array(ref self, depth: int) -> JsonValue {
        Assert(self.current.kind == TokenKind.ArrayBegin);
        let opened = self.current.begin;
        self.advance();
        var result = make_array();
        if (self.current.kind == TokenKind.ArrayEnd) {
            self.advance();
            return result;
        }
        while (true) {
            if (self.at_end()) {
                self.problem = make_error(ErrorCode.UnexpectedEnd, opened, "array opened here was never closed with ']'");
                return result;
            }
            let element = self.parse_value(depth + 1);
            if (not self.ok()) {
                return result;
            }
            result.items.push_back(element);
            if (self.current.kind == TokenKind.Comma) {
                self.advance();
                // A comma directly before ']' is the classic trailing comma.
                if (self.current.kind == TokenKind.ArrayEnd) {
                    if (not self.allow_trailing_commas) {
                        self.raise(ErrorCode.ExpectedValue, "trailing ',' before ']'");
                        return result;
                    }
                    self.advance();
                    return result;
                }
                continue;
            }
            if (self.current.kind == TokenKind.ArrayEnd) {
                self.advance();
                return result;
            }
            self.raise(ErrorCode.ExpectedCommaOrEnd, "expected ',' or ']' after an array element");
            return result;
        }
    }

    // member := string ':' value
    //
    // The key and the value are written through the out parameters so the
    // caller can decide what to do with duplicates.
    fn parse_member(ref self, ref key: string, ref value: JsonValue, depth: int) -> bool {
        if (self.current.kind != TokenKind.String) {
            self.raise(ErrorCode.ExpectedKey, "expected a '\"' quoted member name");
            return false;
        }
        key = self.current.text;
        self.advance();
        if (not self.expect(TokenKind.Colon, ErrorCode.ExpectedColon, "expected ':' after the member name")) {
            return false;
        }
        value = self.parse_value(depth + 1);
        return self.ok();
    }

    // object := '{' '}' | '{' member { ',' member } '}'
    fn parse_object(ref self, depth: int) -> JsonValue {
        Assert(self.current.kind == TokenKind.ObjectBegin);
        let opened = self.current.begin;
        self.advance();
        var result = make_object();
        if (self.current.kind == TokenKind.ObjectEnd) {
            self.advance();
            return result;
        }
        while (true) {
            if (self.at_end()) {
                self.problem = make_error(ErrorCode.UnexpectedEnd, opened, "object opened here was never closed with '}'");
                return result;
            }
            var key: string = "";
            var value = make_null();
            if (not self.parse_member(key, value, depth)) {
                return result;
            }
            let existing = result.index_of(key);
            if (existing >= 0) {
                self.duplicate_keys += 1;
                if (self.reject_duplicate_keys) {
                    var detail: string = "duplicate member name \"";
                    detail.append(key);
                    detail.append("\"");
                    self.problem = make_error(ErrorCode.ExpectedKey, self.current.begin, detail);
                    return result;
                }
                // Last one wins, which is what most readers do in practice.
                result.values[existing] = value;
            } else {
                result.keys.push_back(key);
                result.values.push_back(value);
            }
            if (self.current.kind == TokenKind.Comma) {
                self.advance();
                if (self.current.kind == TokenKind.ObjectEnd) {
                    if (not self.allow_trailing_commas) {
                        self.raise(ErrorCode.ExpectedKey, "trailing ',' before '}'");
                        return result;
                    }
                    self.advance();
                    return result;
                }
                continue;
            }
            if (self.current.kind == TokenKind.ObjectEnd) {
                self.advance();
                return result;
            }
            self.raise(ErrorCode.ExpectedCommaOrEnd, "expected ',' or '}' after a member");
            return result;
        }
    }

    // Entry point. Primes the lookahead, parses one value and, in single
    // value mode, insists that nothing follows it.
    fn parse_document(ref self) -> JsonValue {
        self.advance();
        if (self.at_end()) {
            self.problem = make_error(ErrorCode.UnexpectedEnd, self.current.begin, "the document is empty");
            return make_null();
        }
        var root = self.parse_value(1);
        if (not self.ok()) {
            return root;
        }
        if (self.mode == DocumentMode.SingleValue) {
            if (not self.at_end()) {
                self.raise(ErrorCode.TrailingContent, "expected end of input after the top level value");
            }
        }
        return root;
    }

    var lexer: Lexer;
    var current: Token;
    var problem: Diagnostic;
    var mode: DocumentMode = DocumentMode.SingleValue;
    var depth_limit: int = MAX_DEPTH;
    var deepest: int = 0;
    var tokens_read: int = 0;
    var duplicate_keys: int = 0;
    var allow_trailing_commas: bool = false;
    var reject_duplicate_keys: bool = false;
}

// --- Parse front ends ----------------------------------------------------

// The outcome of a parse: the value plus whatever went wrong producing it.
class ParseResult {

    fn ok(self) -> bool {
        return self.problem.code == ErrorCode.None;
    }

    fn report(self) -> string {
        if (self.ok()) {
            var good: string = "parsed ";
            good.append(self.root.describe());
            good.append(" from ");
            good.append(to_string(self.tokens));
            good.append(" tokens");
            return good;
        }
        return self.problem.render();
    }

    var root: JsonValue;
    var problem: Diagnostic;
    var tokens: int = 0;
    var depth: int = 0;
}

fn parse(source: string) -> ParseResult {
    var parser = Parser({.lexer = Lexer({.source = source})});
    var root = parser.parse_document();
    return ParseResult({.root = root, .problem = parser.problem, .tokens = parser.tokens_read, .depth = parser.deepest});
}

// "JSON with comments" plus trailing commas: the dialect most editors use
// for their configuration files.
fn parse_relaxed(source: string) -> ParseResult {
    var parser = Parser({.lexer = Lexer({.source = source, .allow_comments = true}), .allow_trailing_commas = true});
    var root = parser.parse_document();
    return ParseResult({.root = root, .problem = parser.problem, .tokens = parser.tokens_read, .depth = parser.deepest});
}

// --- Serializer ----------------------------------------------------------

// Compact writes the shortest legal spelling; Pretty puts one element per
// line and indents nested levels.
choice WriteStyle {
    Compact,
    Pretty,
}

// Which characters get a `\uXXXX` escape rather than being copied through.
choice EscapePolicy {
    Minimal,
    EscapeSolidus,
    AsciiOnly,
}

// Escape one string and wrap it in quotes. This is the only place in the
// writer that knows about the '"' and '\\' characters.
fn quote_string(text: string, policy: EscapePolicy) -> string {
    var out: string = "\"";
    var index: int = 0;
    while (index < text.size()) {
        let c = text.at(index);
        let short = encode_escape(c);
        if (not short.empty()) {
            out.append(short);
        } else {
            if (c == '/' and policy == EscapePolicy.EscapeSolidus) {
                out.append("\\/");
            } else {
                var code = int(c);
                if (code < 0) {
                    code += 256;
                }
                var needs_hex: bool = code < CONTROL_LIMIT;
                if (code >= ASCII_LIMIT and policy == EscapePolicy.AsciiOnly) {
                    needs_hex = true;
                }
                if (needs_hex) {
                    out.append("\\u");
                    out.push_back(hex_digit((code >> 12) & 15));
                    out.push_back(hex_digit((code >> 8) & 15));
                    out.push_back(hex_digit((code >> 4) & 15));
                    out.push_back(hex_digit(code & 15));
                } else {
                    out.push_back(c);
                }
            }
        }
        index += 1;
    }
    out.append("\"");
    return out;
}

// Spell a scaled number back out. Whole numbers print without an exponent;
// anything else keeps the `<mantissa>e<exponent>` form, which is legal JSON.
fn spell_number(value: Number) -> string {
    var out: string = "";
    if (value.negative) {
        out.append("-");
    }
    out.append(to_string(value.mantissa));
    if (value.exponent > 0) {
        // Small positive exponents are cheaper to read as trailing zeroes.
        if (value.exponent <= 6) {
            var pad = value.exponent;
            while (pad > 0) {
                out.append("0");
                pad -= 1;
            }
            return out;
        }
        out.append("e");
        out.append(to_string(value.exponent));
        return out;
    }
    if (value.exponent < 0) {
        out.append("e-");
        out.append(to_string(-value.exponent));
    }
    return out;
}

class Writer {

    // One indentation unit repeated `level` times.
    fn indent(self, level: int) -> string {
        var out: string = "";
        if (self.style == WriteStyle.Compact) {
            return out;
        }
        var step: int = 0;
        while (step < level) {
            out.append(self.indent_unit);
            step += 1;
        }
        return out;
    }

    fn newline(self) -> string {
        if (self.style == WriteStyle.Compact) {
            return "";
        }
        return "\n";
    }

    // ':' in compact mode, ": " when pretty printing.
    fn key_separator(self) -> string {
        if (self.style == WriteStyle.Compact) {
            return ":";
        }
        return ": ";
    }

    fn write_value(ref self, ref out: string, value: JsonValue, level: int) {
        if (value.kind == JsonKind.Null) {
            out.append("null");
            return;
        }
        if (value.kind == JsonKind.Bool) {
            if (value.boolean) {
                out.append("true");
            } else {
                out.append("false");
            }
            return;
        }
        if (value.kind == JsonKind.Number) {
            out.append(spell_number(value.number));
            return;
        }
        if (value.kind == JsonKind.String) {
            out.append(quote_string(value.text, self.policy));
            return;
        }
        if (value.kind == JsonKind.Array) {
            self.write_array(out, value, level);
            return;
        }
        if (value.kind == JsonKind.Object) {
            self.write_object(out, value, level);
            return;
        }
        out.append("null");
    }

    fn write_array(ref self, ref out: string, value: JsonValue, level: int) {
        if (value.items.empty()) {
            out.append("[]");
            return;
        }
        out.append("[");
        out.append(self.newline());
        var index: int = 0;
        while (index < value.items.size()) {
            out.append(self.indent(level + 1));
            self.write_value(out, value.items[index], level + 1);
            if (index + 1 < value.items.size()) {
                out.append(",");
            }
            out.append(self.newline());
            index += 1;
        }
        out.append(self.indent(level));
        out.append("]");
    }

    fn write_object(ref self, ref out: string, value: JsonValue, level: int) {
        if (value.keys.empty()) {
            out.append("{}");
            return;
        }
        out.append("{");
        out.append(self.newline());
        var slot: int = 0;
        while (slot < value.keys.size()) {
            out.append(self.indent(level + 1));
            out.append(quote_string(value.keys[slot], self.policy));
            out.append(self.key_separator());
            self.write_value(out, value.values[slot], level + 1);
            if (slot + 1 < value.keys.size()) {
                out.append(",");
            }
            out.append(self.newline());
            slot += 1;
        }
        out.append(self.indent(level));
        out.append("}");
    }

    fn write_document(ref self, value: JsonValue) -> string {
        var out: string = "";
        self.write_value(out, value, 0);
        if (self.trailing_newline and self.style == WriteStyle.Pretty) {
            out.append("\n");
        }
        return out;
    }

    var style: WriteStyle = WriteStyle.Compact;
    var policy: EscapePolicy = EscapePolicy.Minimal;
    var indent_unit: string = "  ";
    var trailing_newline: bool = true;
}

// `{"a":[1,2]}` on a single line.
fn write_compact(value: JsonValue) -> string {
    var writer = Writer({.style = WriteStyle.Compact, .trailing_newline = false});
    return writer.write_document(value);
}

// Two space indentation, one element per line, trailing newline.
fn write_pretty(value: JsonValue) -> string {
    var writer = Writer({.style = WriteStyle.Pretty, .indent_unit = "  "});
    return writer.write_document(value);
}

// Read and write back out, which normalises spacing, escapes and key order
// handling. Returns the empty string when the input does not parse.
fn reformat(source: string, style: WriteStyle) -> string {
    let result = parse(source);
    if (not result.ok()) {
        return "";
    }
    if (style == WriteStyle.Pretty) {
        return write_pretty(result.root);
    }
    return write_compact(result.root);
}

// --- Tree walking utilities ----------------------------------------------

// Two numbers are equal when they denote the same value, not when they were
// spelled the same way: 1e2, 100 and 1000e-1 all compare equal. Normalising
// means stripping trailing zeroes from the mantissa and raising the
// exponent to match.
fn normalize_number(value: Number) -> Number {
    var out = *value;
    while (out.mantissa != 0 and out.mantissa % 10 == 0) {
        out.mantissa = out.mantissa / 10;
        out.exponent += 1;
    }
    if (out.mantissa == 0) {
        out.exponent = 0;
        out.negative = false;
    }
    return out;
}

fn number_equal(left: Number, right: Number) -> bool {
    let a = normalize_number(left);
    let b = normalize_number(right);
    if (a.mantissa != b.mantissa) {
        return false;
    }
    if (a.exponent != b.exponent) {
        return false;
    }
    return a.negative == b.negative;
}

// Structural equality. Arrays compare element by element in order; objects
// compare by member name, so key order does not matter.
fn deep_equal(left: JsonValue, right: JsonValue) -> bool {
    if (left.kind != right.kind) {
        return false;
    }
    if (left.kind == JsonKind.Null) {
        return true;
    }
    if (left.kind == JsonKind.Bool) {
        return left.boolean == right.boolean;
    }
    if (left.kind == JsonKind.Number) {
        return number_equal(left.number, right.number);
    }
    if (left.kind == JsonKind.String) {
        return left.text == right.text;
    }
    if (left.kind == JsonKind.Array) {
        if (left.items.size() != right.items.size()) {
            return false;
        }
        var index: int = 0;
        while (index < left.items.size()) {
            if (not deep_equal(left.items[index], right.items[index])) {
                return false;
            }
            index += 1;
        }
        return true;
    }
    // Objects: same member count, and every left member has an equal twin.
    if (left.keys.size() != right.keys.size()) {
        return false;
    }
    var slot: int = 0;
    while (slot < left.keys.size()) {
        let other = right.index_of(left.keys[slot]);
        if (other < 0) {
            return false;
        }
        if (not deep_equal(left.values[slot], right.values[other])) {
            return false;
        }
        slot += 1;
    }
    return true;
}

// --- JSON pointers -------------------------------------------------------

// Inside a pointer token, '~' and '/' are written "~0" and "~1". Undo that.
fn unescape_pointer_token(token: string) -> string {
    var out: string = "";
    var index: int = 0;
    while (index < token.size()) {
        let c = token.at(index);
        if (c == '~' and index + 1 < token.size()) {
            let next = token.at(index + 1);
            if (next == '0') {
                out.push_back('~');
                index += 2;
                continue;
            }
            if (next == '1') {
                out.push_back('/');
                index += 2;
                continue;
            }
        }
        out.push_back(c);
        index += 1;
    }
    return out;
}

fn escape_pointer_token(token: string) -> string {
    var out: string = "";
    var index: int = 0;
    while (index < token.size()) {
        let c = token.at(index);
        if (c == '~') {
            out.append("~0");
        } else {
            if (c == '/') {
                out.append("~1");
            } else {
                out.push_back(c);
            }
        }
        index += 1;
    }
    return out;
}

// "/a/b/0" becomes ["a", "b", "0"]. The empty pointer "" selects the root
// and yields no tokens at all.
fn split_pointer(path: string) -> vector(string) {
    var tokens: vector(string);
    if (path.empty()) {
        return tokens;
    }
    var current: string = "";
    var index: int = 0;
    if (path.at(0) == '/') {
        index = 1;
    }
    while (index < path.size()) {
        let c = path.at(index);
        if (c == '/') {
            tokens.push_back(unescape_pointer_token(current));
            current.clear();
        } else {
            current.push_back(c);
        }
        index += 1;
    }
    tokens.push_back(unescape_pointer_token(current));
    return tokens;
}

fn join_pointer(tokens: vector(string)) -> string {
    var out: string = "";
    for (token in tokens) {
        out.append("/");
        out.append(escape_pointer_token(token));
    }
    return out;
}

fn is_index_token(token: string) -> bool {
    if (token.empty()) {
        return false;
    }
    var index: int = 0;
    while (index < token.size()) {
        if (not is_digit(token.at(index))) {
            return false;
        }
        index += 1;
    }
    // "01" is not a valid array index in a JSON pointer.
    if (token.size() > 1 and token.at(0) == '0') {
        return false;
    }
    return true;
}

// Walk `tokens` starting at `index`. Returns the node itself once the
// tokens run out, and stops early on a miss, so pair this with
// pointer_exists() when the difference matters.
fn resolve_tokens(ref node: JsonValue, tokens: vector(string), index: int) -> ref JsonValue {
    if (index >= tokens.size()) {
        return node;
    }
    let step = tokens[index];
    if (node.kind == JsonKind.Object) {
        return resolve_tokens(node.member(step), tokens, index + 1);
    }
    if (node.kind == JsonKind.Array) {
        if (not is_index_token(step)) {
            return node;
        }
        return resolve_tokens(node.at(parse_int(step)), tokens, index + 1);
    }
    return node;
}

// json::at(root, "/servers/0/host")
fn at_pointer(ref root: JsonValue, path: string) -> ref JsonValue {
    let tokens = split_pointer(path);
    return resolve_tokens(root, tokens, 0);
}

fn tokens_exist(node: JsonValue, tokens: vector(string), index: int) -> bool {
    if (index >= tokens.size()) {
        return true;
    }
    let step = tokens[index];
    if (node.kind == JsonKind.Object) {
        let slot = node.index_of(step);
        if (slot < 0) {
            return false;
        }
        return tokens_exist(node.values[slot], tokens, index + 1);
    }
    if (node.kind == JsonKind.Array) {
        if (not is_index_token(step)) {
            return false;
        }
        let position = parse_int(step);
        if (position >= node.items.size()) {
            return false;
        }
        return tokens_exist(node.items[position], tokens, index + 1);
    }
    return false;
}

fn pointer_exists(root: JsonValue, path: string) -> bool {
    let tokens = split_pointer(path);
    return tokens_exist(root, tokens, 0);
}

// --- Merge ---------------------------------------------------------------

// A merge patch in the style of RFC 7386: objects are merged member by
// member, a null member deletes the key, and everything else replaces the
// target outright.
fn merge_patch(target: JsonValue, patch: JsonValue) -> JsonValue {
    if (patch.kind != JsonKind.Object) {
        return *patch;
    }
    var out = make_object();
    if (target.kind == JsonKind.Object) {
        var slot: int = 0;
        while (slot < target.keys.size()) {
            out.keys.push_back(target.keys[slot]);
            out.values.push_back(target.values[slot]);
            slot += 1;
        }
    }
    var index: int = 0;
    while (index < patch.keys.size()) {
        let key = patch.keys[index];
        let update = patch.values[index];
        if (update.kind == JsonKind.Null) {
            Discard(out.remove_member(key));
        } else {
            let existing = out.index_of(key);
            if (existing >= 0) {
                out.values[existing] = merge_patch(out.values[existing], update);
            } else {
                out.keys.push_back(key);
                out.values.push_back(merge_patch(make_null(), update));
            }
        }
        index += 1;
    }
    return out;
}

// Merge two documents given as text and hand back the pretty printed result.
fn merge_sources(base_: string, overlay: string) -> string {
    let left = parse(base_);
    if (not left.ok()) {
        return left.problem.render();
    }
    let right = parse(overlay);
    if (not right.ok()) {
        return right.problem.render();
    }
    let merged = merge_patch(left.root, right.root);
    Prove(merged.kind == JsonKind.Object or merged.kind == right.root.kind);
    return write_pretty(merged);
}

