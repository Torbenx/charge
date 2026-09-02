#!/usr/bin/env python3
"""Derive the expression-only benchmark corpus in `benchmark-expr/` from `benchmark/`.

The target grammar knows only

  * prefix unary operators   ! ~ + - ++ -- *
  * binary operators         , + - * & ^ | / % << >> && || != == < <= > >=
  * postfix operators        ++ --
  * assignments              = += -= *= &= ^= |= /= %= <<= >>= &&= ||=
  * `;` between expressions

Everything the full charge grammar offers on top of that -- statements, call
expressions, index expressions, template parameterisation, member and static
access, declarations, types -- is rewritten away.  Only function bodies
survive; the comments of the input are carried over verbatim and in place.

Run `./benchmark-expr/generate.py --help` for the knobs.
"""

import argparse
import dataclasses
import pathlib
import random
import sys

# --------------------------------------------------------------- source files

# Same list and order as `benchmark/benchmark.py`, so that the concatenated
# corpus of both directories describes the same programs.
SOURCE_FILES = [
    "std.chrg",
    "b_tree.chrg",
    "bignum.chrg",
    "bytecode_vm.chrg",
    "chess_movegen.chrg",
    "compression.chrg",
    "expression_language.chrg",
    "graph_algorithms.chrg",
    "json.chrg",
    "particle_sim.chrg",
    "red_black_tree.chrg",
    "reference.chrg",
    "regex_engine.chrg",
    "slab_allocator.chrg",
    "trie_autocomplete.chrg",
    "utf8_codec.chrg",
]

# ---------------------------------------------------------------------- lexer

# Longest match wins, so the table is ordered by descending length.
PUNCTUATION = sorted(
    [
        "(", ")", "[", "]", "{", "}", "~", ",", ".", ";", ":", "::",
        "&", "|", "<", ">", "+", "-", "^", "*", "%", "/", "!",
        "&&", "||", "<<", ">>", "++", "--",
        "&=", "|=", "<=", ">=", "+=", "-=", "^=", "*=", "%=", "/=", "!=",
        "&&=", "||=", "<<=", ">>=",
        "=", "==", "=>", "<=>", "->",
    ],
    key=len,
    reverse=True,
)

KEYWORDS = {
    "assert", "break", "catch", "const", "continue", "destroy", "discard",
    "do", "elif", "else", "for", "if", "impl", "in", "let", "loop", "prove",
    "return", "shared", "static", "try", "unique", "var", "while",
}

# Identifiers that introduce a declaration; they never occur inside a body.
DECLARATION_WORDS = {"fn", "struct", "enum", "namespace", "template", "trait", "impl"}

WORD_START = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_$#")
WORD_BODY = WORD_START | set("0123456789")
NUMBER_BODY = set("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.")

WORD = "word"
NUMBER = "number"
STRING = "string"
CHARACTER = "character"
PUNCT = "punctuation"


@dataclasses.dataclass
class Token:
    kind: str
    text: str
    trivia: str          # whitespace and comments preceding the token
    line: int
    out: str = ""        # replacement text; assigned by the rewriter
    synth: bool = False  # `out` is an operator spliced in for removed syntax

    @property
    def isWord(self):
        return self.kind == WORD

    @property
    def isKeyword(self):
        return self.kind == WORD and self.text in KEYWORDS

    def isPunct(self, *texts):
        return self.kind == PUNCT and self.text in texts

    def isText(self, *texts):
        return self.text in texts and self.kind in (PUNCT, WORD)


class LexError(Exception):
    pass


def lex(text):
    """Split `text` into tokens, attaching all preceding trivia to each token."""
    tokens = []
    pos = 0
    line = 1
    size = len(text)
    triviaStart = 0
    while pos < size:
        c = text[pos]
        if c in " \t\r\n":
            if c == "\n":
                line += 1
            pos += 1
            continue
        if c == "/" and pos + 1 < size and text[pos + 1] == "/":
            end = text.find("\n", pos)
            pos = size if end < 0 else end
            continue
        if c == "/" and pos + 1 < size and text[pos + 1] == "*":
            end = text.find("*/", pos + 2)
            if end < 0:
                raise LexError(f"unterminated block comment on line {line}")
            line += text.count("\n", pos, end)
            pos = end + 2
            continue

        start = pos
        if c in WORD_START:
            while pos < size and text[pos] in WORD_BODY:
                pos += 1
            kind = WORD
        elif c.isdigit():
            while pos < size and text[pos] in NUMBER_BODY:
                pos += 1
            kind = NUMBER
        elif c in "\"'":
            pos += 1
            while pos < size and text[pos] != c:
                pos += 2 if text[pos] == "\\" else 1
            if pos >= size:
                raise LexError(f"unterminated literal on line {line}")
            pos += 1
            kind = STRING if c == '"' else CHARACTER
        else:
            for punctuation in PUNCTUATION:
                if text.startswith(punctuation, pos):
                    pos += len(punctuation)
                    break
            else:
                raise LexError(f"unexpected character {c!r} on line {line}")
            kind = PUNCT

        tokens.append(Token(kind, text[start:pos], text[triviaStart:start], line))
        line += text.count("\n", start, pos)
        triviaStart = pos

    # A sentinel carrying the trailing trivia keeps the tail comments alive.
    tokens.append(Token("eof", "", text[triviaStart:], line))
    return tokens

# ------------------------------------------------------- structure: fn bodies


def matching(tokens, index):
    """Index of the bracket closing the one at `index`."""
    opening = tokens[index].text
    closing = {"(": ")", "[": "]", "{": "}"}[opening]
    depth = 0
    while index < len(tokens):
        if tokens[index].isPunct(opening):
            depth += 1
        elif tokens[index].isPunct(closing):
            depth -= 1
            if depth == 0:
                return index
        index += 1
    raise LexError(f"unbalanced {opening}")


def findFunctionBodies(tokens):
    """Yield `(name, kind, open, close)` for every function body in `tokens`.

    A body is either a block, `fn f(...) : { ... }`, or an expression,
    `fn f(...) => ... ;`.  For a block `open`/`close` are the braces, for an
    expression they are the `=>` and the `;`.
    """
    index = 0
    overloads = 0
    while index < len(tokens):
        if not (tokens[index].isWord and tokens[index].text == "fn"):
            index += 1
            continue
        name = tokens[index + 1].text if tokens[index + 1].isWord else "anonymous"
        if name == "impl":
            # `fn impl (a + b)(...)`: an operator overload has no plain name.
            overloads += 1
            name = f"impl_{overloads}"
        # The parameter list, the return type and any `{...}` parameterisation
        # in it are balanced, so the first `:`, `=>` or `;` we reach at depth
        # zero ends the signature.
        cursor = index + 1
        while cursor < len(tokens) and not tokens[cursor].isPunct(":", "=>", ";"):
            if tokens[cursor].isPunct("(", "[", "{"):
                cursor = matching(tokens, cursor)
            cursor += 1
        if cursor >= len(tokens):
            raise LexError(f"function {name} has no body")
        if tokens[cursor].isPunct(";"):
            index = cursor + 1                  # a declaration without a body
            continue
        if tokens[cursor].isPunct("=>"):
            end = cursor + 1
            while end < len(tokens) and not tokens[end].isPunct(";"):
                if tokens[end].isPunct("(", "[", "{"):
                    end = matching(tokens, end)
                end += 1
            if end >= len(tokens):
                raise LexError(f"function {name} has an unterminated body")
            yield name, "arrow", cursor, end
            index = end + 1
            continue
        if not tokens[cursor + 1].isPunct("{"):
            raise LexError(f"function {name} has no body")
        openBrace = cursor + 1
        closeBrace = matching(tokens, openBrace)
        yield name, "brace", openBrace, closeBrace
        index = closeBrace + 1

# ------------------------------------------------------------------ operators

BINARY_PUNCTUATION = [
    "+", "-", "*", "/", "%", "&", "^", "|", "<<", ">>", "&&", "||",
    "==", "!=", "<", "<=", ">", ">=",
]


def endsExpression(token, previous):
    """Coarse `could an operand have just ended here?` test."""
    if token.isPunct("++", "--"):
        return previous                # postfix keeps, prefix does not start
    if token.kind in (NUMBER, STRING, CHARACTER):
        return True
    if token.kind == WORD:
        return not token.isKeyword
    return token.isPunct(")", "]", "}")


def binaryHistogram(tokens):
    """Count how often each binary operator is used in `tokens`."""
    counts = dict.fromkeys(BINARY_PUNCTUATION, 0)
    previous = False
    for token in tokens:
        if previous and token.kind == PUNCT and token.text in counts:
            counts[token.text] += 1
        previous = endsExpression(token, previous)
    return counts


class OperatorSource:
    """Deterministic supply of the operator that replaces removed syntax.

    `comma` treats `,` as the binary operator it already is in an argument
    list.  `weighted` instead draws from the binary-operator frequency of the
    input corpus, so that folding calls, indices and member accesses away does
    not skew the operator mix a lexer or parser sees.
    """

    def __init__(self, mode, weights, seed=0):
        self.mode = mode
        self.random = random.Random(seed)
        self.population = list(weights)
        self.weights = [max(1, count) for count in weights.values()]

    def next(self):
        if self.mode == "comma":
            return ","
        if self.mode == "fixed":
            return "+"
        return self.random.choices(self.population, self.weights)[0]

# ------------------------------------------------------------------- rewriter

DELETE = ""

# Statement keywords that survive into the reduced grammar.
STATEMENT_KEYWORDS = {
    "if", "else", "while", "do", "loop", "for", "in", "break", "continue",
    "return", "let", "var", "destroy", "discard", "prove", "assert",
}

OPERATOR_PUNCTUATION = set(BINARY_PUNCTUATION) | {"!", "~", "++", "--"}

UPDATE_PUNCTUATION = {
    "=", "+=", "-=", "*=", "&=", "^=", "|=", "/=", "%=", "<<=", ">>=",
    "&&=", "||=",
}


@dataclasses.dataclass
class Frame:
    kind: str            # call | index | parameterize | group | block
    atArgument: bool = True
    ifDepth: int = 0     # open `if ... => ... else ...` expressions


class Rewriter:
    def __init__(self, options, operators, report):
        self.options = options
        self.operators = operators
        self.report = report
        # A stream of its own, so that toggling --increments does not shift the
        # operators the fold uses.
        self.random = random.Random(options.seed + 1)

    @property
    def expressionsOnly(self):
        return self.options.grammar == "expressions"

    def run(self, tokens, first, last):
        """Assign `out` for every token of a function body in `[first, last)`."""
        self.tokens = tokens
        self.first = first
        self.stack = [Frame("block")]
        self.exprEnd = False
        self.statementStart = None
        index = first
        while index < last:
            begin = index
            index = self.step(index, last)
            for cursor in range(begin, index):
                if tokens[cursor].out == ";":
                    self.statementStart = None
                elif tokens[cursor].out and self.statementStart is None:
                    self.statementStart = cursor

    # -- helpers

    @property
    def frame(self):
        return self.stack[-1]

    @property
    def inExpression(self):
        return self.frame.kind != "block"

    def skipType(self, index, last, stops):
        """Delete a type annotation starting at `:` until one of `stops`."""
        tokens = self.tokens
        while index < last and not tokens[index].isText(*stops):
            if tokens[index].isPunct("(", "[", "{"):
                end = matching(tokens, index)
                for token in tokens[index:end + 1]:
                    token.out = DELETE
                index = end
            tokens[index].out = DELETE
            index += 1
        return index

    # -- the main dispatch

    def step(self, index, last):
        tokens = self.tokens
        token = tokens[index]
        text = token.text
        token.out = text          # keep by default
        nextIndex = index + 1

        if token.kind in (NUMBER, STRING, CHARACTER):
            token.out = self.literal(token)
            self.exprEnd = True
            self.frame.atArgument = False
            return nextIndex

        if token.kind == WORD:
            return self.word(index, last)

        # punctuation from here on
        if text in OPERATOR_PUNCTUATION:
            if text in ("++", "--"):
                pass                       # postfix or prefix; `exprEnd` is unchanged
            else:
                self.exprEnd = False
            self.frame.atArgument = False
            return nextIndex

        if text in UPDATE_PUNCTUATION:
            if text in ("+=", "-=") and self.increment(index, last):
                self.exprEnd = True
                return index + 2
            self.exprEnd = False
            return nextIndex

        if text == ";":
            self.exprEnd = False
            self.frame.atArgument = True
            return nextIndex

        if text == ",":
            if self.inExpression:
                self.splice(token)
            else:
                token.out = DELETE
            self.exprEnd = False
            self.frame.atArgument = True
            return nextIndex

        if text in (".", "::"):
            return self.access(index)

        if text == ":":
            # Inside an expression this is the remainder of a designated
            # argument.  At statement level it separates a condition from its
            # body, which in the expression-only grammar ends the expression.
            if self.inExpression:
                token.out = DELETE
            else:
                token.out = ";" if self.expressionsOnly else ":"
            self.exprEnd = False
            return nextIndex

        if text == "=>":
            # `if c => a else b` and context expressions both fold into a binary
            # operator; the operands stay.
            self.splice(token)
            self.exprEnd = False
            return nextIndex

        if text in ("(", "[", "{"):
            return self.open(index)

        if text in (")", "]", "}"):
            return self.close(index)

        if text == "->":
            self.report("arrow inside a function body", token)
            token.out = DELETE
            return nextIndex

        self.report(f"unhandled punctuation {text!r}", token)
        token.out = DELETE
        return nextIndex

    def splice(self, token):
        token.out = self.operators.next()
        token.synth = True

    def increment(self, index, last):
        """Turn a whole statement `name += 1;` into `name++;` or `++name;`.

        The input corpus spells every step that way and never uses `++` or
        `--`, so without this the increment operators of the reduced grammar
        would go untested.
        """
        if self.options.increments != "synthesize":
            return False
        tokens = self.tokens
        start = self.statementStart
        if start is None or start != index - 1 or tokens[start].kind != WORD:
            return False
        if index + 2 >= last:
            return False
        if not (tokens[index + 1].kind == NUMBER and tokens[index + 1].text == "1"):
            return False
        if not tokens[index + 2].isPunct(";"):
            return False
        operator = "++" if tokens[index].text == "+=" else "--"
        if self.random.random() < 0.5:
            tokens[index].out = operator          # postfix: `name++`
        else:
            tokens[start].out = operator + tokens[start].out   # prefix: `++name`
            tokens[index].out = DELETE
        tokens[index + 1].out = DELETE
        # The blanks around `+= 1` would otherwise be left dangling.
        tokens[index].trivia = ""
        tokens[index + 1].trivia = ""
        return True

    def literal(self, token):
        if token.kind == STRING and self.options.strings == "shorten":
            return '"s"'
        return token.text

    def word(self, index, last):
        tokens = self.tokens
        token = tokens[index]
        text = token.text

        if text in DECLARATION_WORDS:
            self.report(f"declaration keyword {text!r} inside a function body", token)
            token.out = DELETE
            return index + 1

        if text == "if" and (self.inExpression or self.expressionsOnly):
            # An `if` expression: drop the keyword, `=>` and `else` become
            # binary operators.
            token.out = DELETE
            self.frame.ifDepth += 1
            self.exprEnd = False
            return index + 1

        if text == "else" and self.inExpression and self.frame.ifDepth > 0:
            self.splice(token)
            self.frame.ifDepth -= 1
            self.exprEnd = False
            return index + 1

        if text in ("let", "var"):
            # `let name : type = value ;` -> `let name = value ;`
            if self.expressionsOnly:
                token.out = DELETE
            name = index + 1
            if name < last and tokens[name].isWord:
                tokens[name].out = tokens[name].text
                if name + 1 < last and tokens[name + 1].isPunct(":"):
                    tokens[name + 1].out = DELETE
                    end = self.skipType(name + 2, last, ("=", ";", "in"))
                    self.exprEnd = False
                    return end
                self.exprEnd = True
                return name + 1
            self.exprEnd = False
            return index + 1

        if text == "for":
            # `for name : type in range :` -> `for name in range :`
            if self.expressionsOnly:
                token.out = DELETE
            name = index + 1
            if name < last and tokens[name].isWord and tokens[name].text == "var":
                tokens[name].out = DELETE if self.expressionsOnly else "var"
                name += 1
            if name < last and tokens[name].isWord:
                tokens[name].out = tokens[name].text
                if name + 1 < last and tokens[name + 1].isPunct(":"):
                    tokens[name + 1].out = DELETE
                    end = self.skipType(name + 2, last, ("in", ":"))
                    self.exprEnd = False
                    return end
                self.exprEnd = False
                return name + 1
            self.exprEnd = False
            return index + 1

        if text in ("const", "shared", "unique"):
            # Reference modifiers only appear in types, which are gone by now.
            self.report(f"stray reference modifier {text!r}", token)
            token.out = DELETE
            return index + 1

        if text in STATEMENT_KEYWORDS or text in KEYWORDS:
            if self.expressionsOnly:
                # `in` joins the loop variable to the range, every other
                # statement keyword simply goes away.
                if text == "in":
                    self.splice(token)
                else:
                    token.out = DELETE
            self.exprEnd = False
            self.frame.atArgument = False
            return index + 1

        # A plain identifier: the operand of the reduced grammar.  A designated
        # argument (`name:` in a call) loses its name.
        if self.inExpression and self.frame.atArgument:
            following = index + 1
            if following < last and tokens[following].isPunct(":"):
                token.out = DELETE
                tokens[following].out = DELETE
                self.exprEnd = False
                return following + 1

        self.exprEnd = True
        self.frame.atArgument = False
        return index + 1

    def access(self, index):
        """Rewrite `a.b`, `a::b` and the implicit-self `.b`."""
        tokens = self.tokens
        token = tokens[index]
        member = index + 1
        implicitSelf = not self.exprEnd

        if implicitSelf:
            # `.count` -> `count`, or `self OP count`
            if self.options.implicitSelf == "drop":
                token.out = DELETE
            else:
                token.out = f"self {self.operators.next()}"
                token.synth = True
            self.exprEnd = False
            self.frame.atArgument = False
            return index + 1

        if self.options.access == "binary":
            self.splice(token)
            self.exprEnd = False
        elif self.options.access == "merge":
            # `a.b` -> `a_b`: fold the member into the operand before it.  If
            # the access does not sit on an operand -- `(a + b).size` with the
            # parentheses gone -- fall back to splicing an operator in.
            previous = index - 1
            while previous >= self.first and tokens[previous].out == DELETE:
                previous -= 1
            mergeable = (previous >= self.first
                         and tokens[previous].kind in (WORD, NUMBER, STRING, CHARACTER)
                         and tokens[member].isWord
                         and tokens[previous].out.isidentifier())
            if mergeable:
                tokens[previous].out = tokens[previous].out + "_" + tokens[member].text
                token.out = DELETE
                tokens[member].out = DELETE
                self.exprEnd = True
                self.frame.atArgument = False
                return member + 1
            self.splice(token)
            self.exprEnd = False
        else:  # drop
            token.out = DELETE
            if tokens[member].isWord:
                tokens[member].out = DELETE
                self.exprEnd = True
                return member + 1
            self.exprEnd = True
        self.frame.atArgument = False
        return index + 1

    def open(self, index):
        tokens = self.tokens
        token = tokens[index]
        text = token.text
        closing = matching(tokens, index)
        empty = closing == index + 1

        if text == "(" and not self.exprEnd:
            self.stack.append(Frame("group"))
            token.out = "(" if self.options.parentheses == "keep" else DELETE
            self.exprEnd = False
            return index + 1

        if text == "{" and not self.exprEnd:
            self.stack.append(Frame("block"))
            token.out = DELETE if self.expressionsOnly else "{"
            self.exprEnd = False
            return index + 1

        if text == "[" and not self.exprEnd:
            # A borrow list `&const[a, b]`; only valid in types.
            self.report("borrow list inside a function body", token)
            self.stack.append(Frame("index"))
            token.out = DELETE
            return index + 1

        kind = {"(": "call", "[": "index", "{": "parameterize"}[text]
        self.stack.append(Frame(kind))
        # `f()` collapses to `f`, `f(a, b)` to `f OP a OP b`.
        if empty:
            token.out = DELETE
        else:
            self.splice(token)
        self.exprEnd = False
        return index + 1

    def close(self, index):
        token = self.tokens[index]
        if len(self.stack) == 1:
            self.report("unbalanced closing bracket", token)
            token.out = DELETE
            return index + 1
        frame = self.stack.pop()
        if frame.kind == "block":
            token.out = DELETE if self.expressionsOnly else "}"
            self.exprEnd = False
        elif frame.kind == "group":
            token.out = ")" if self.options.parentheses == "keep" else DELETE
            self.exprEnd = True
        else:
            token.out = DELETE
            self.exprEnd = True
        self.frame.atArgument = False
        return index + 1

# ------------------------------------------------------------------ rendering


# Characters that combine into a longer operator when they end up adjacent.
COMBINING = set("!~+-*&^|/%<>=:")


def glues(left, right):
    """True if emitting `right` straight after `left` would lex as one token."""
    if not left or not right:
        return False
    return ((left[-1] in COMBINING and right[0] in COMBINING)
            or (left[-1] in WORD_BODY and right[0] in WORD_BODY))


def carriesLayout(trivia):
    """True if the trivia holds a comment or a line break worth preserving."""
    return "\n" in trivia or trivia.strip() != ""


def render(tokens):
    """Concatenate the rewritten tokens, keeping the layout of the input.

    A deleted token hands its comments and line breaks on to the next token
    that survives, but not the horizontal whitespace around it -- that would
    only leave ragged gaps behind.
    """
    parts = []
    layout = []
    space = False
    previousSynth = False
    previousOut = ""
    for token in tokens:
        if carriesLayout(token.trivia):
            layout.append(token.trivia)
        elif token.trivia:
            space = True
        if not token.out:
            continue
        if layout:
            parts.extend(layout)
            layout = []
            previousOut = ""
        elif space or token.synth or previousSynth or glues(previousOut, token.out):
            parts.append(" ")
        space = False
        parts.append(token.out)
        previousOut = token.out
        previousSynth = token.synth
    parts.extend(layout)
    return "".join(parts)


def indentationOf(line):
    return line[:len(line) - len(line.lstrip())]


def realign(lines):
    """Re-indent comments whose statement was deleted out from under them.

    A trailing comment such as `static MASK: int = 255;  // 0xFF` ends up alone
    on its line once the declaration is gone, still sitting at the column the
    code used to end in.  A comment line whose indentation matches neither of
    its neighbours is such an orphan and joins the line above it.
    """
    for index, line in enumerate(lines):
        if not line.lstrip().startswith("//"):
            continue
        previous = next((other for other in reversed(lines[:index]) if other.strip()), None)
        if previous is None:
            continue                # nothing was deleted above this comment
        following = next((other for other in lines[index + 1:] if other.strip()), None)
        neighbours = [indentationOf(other) for other in (previous, following) if other is not None]
        if indentationOf(line) in neighbours:
            continue
        lines[index] = indentationOf(previous) + line.lstrip()
    return lines


def tidy(text):
    """Strip the whitespace and the blank lines the deleted tokens left behind."""
    lines = realign([line.rstrip() for line in text.split("\n")])
    result = []
    blanks = 0
    for line in lines:
        if line:
            blanks = 0
            result.append(line)
            continue
        blanks += 1
        if blanks <= 1:
            result.append(line)
    while result and not result[-1]:
        result.pop()
    return "\n".join(result) + "\n"


def stripComments(text, blocks=False):
    """Remove line comments, or turn them into block comments."""
    out = []
    for line in text.split("\n"):
        stripped = line.lstrip()
        if not stripped.startswith("//"):
            out.append(line)
            continue
        if not blocks:
            continue
        indent = line[:len(line) - len(stripped)]
        out.append(f"{indent}/*{stripped[2:]} */")
    return "\n".join(out)

def dropEmptyStatements(tokens):
    """Remove the `;` of statements whose keyword was the only thing in them.

    `break;` and `return;` leave nothing behind, and `loop:` or `else:` turn
    their colon into a terminator with no expression in front of it.
    """
    content = False
    for token in tokens:
        if not token.out:
            continue
        if token.out == ";":
            if not content:
                token.out = DELETE
            content = False
        else:
            content = True


# ------------------------------------------------------------------- checking


def check(text, options):
    """Verify that the result really is within the reduced grammar."""
    allowedPunctuation = set(OPERATOR_PUNCTUATION) | set(UPDATE_PUNCTUATION) | {";"}
    if options.grammar == "expressions":
        allowedWords = set()
        allowedPunctuation |= {","}
    else:
        allowedWords = STATEMENT_KEYWORDS | {"var"}
        allowedPunctuation |= {":", "{", "}", ","}
    if options.parentheses == "keep":
        allowedPunctuation |= {"(", ")"}
    offenders = {}
    for token in lex(text):
        if token.kind == PUNCT and token.text not in allowedPunctuation:
            offenders[token.text] = offenders.get(token.text, 0) + 1
        elif token.kind == WORD and token.text in KEYWORDS and token.text not in allowedWords:
            offenders[token.text] = offenders.get(token.text, 0) + 1
    return offenders


# ----------------------------------------------------------------- conversion


def convert(text, options, operators, report):
    tokens = lex(text)
    for token in tokens:
        token.out = DELETE          # everything is dropped unless kept below
    rewriter = Rewriter(options, operators, report)
    bodies = 0
    wrapper = "flat" if options.grammar == "expressions" else options.wrapper
    for name, kind, open, close in findFunctionBodies(tokens):
        if close == open + 1:
            continue                # an empty body contributes nothing
        rewriter.run(tokens, open + 1, close)
        # An expression body `=> e ;` becomes the statement `e ;`, so its `;`
        # is kept while a block body keeps its braces.
        header = {"flat": DELETE, "block": "{", "fn": f"fn {name}: {{"}[wrapper]
        footer = "}" if wrapper != "flat" else DELETE
        if kind == "arrow":
            tokens[open].out = header
            tokens[close].out = f"; {footer}" if footer else ";"
        else:
            tokens[open].out = header
            tokens[close].out = footer
        bodies += 1
    dropEmptyStatements(tokens)
    return tidy(render(tokens)), bodies


def main(argv=None):
    root = pathlib.Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=pathlib.Path, default=root.parent / "benchmark",
                        help="directory holding the full-grammar corpus")
    parser.add_argument("--output", type=pathlib.Path, default=root,
                        help="directory to write the reduced corpus to")
    parser.add_argument("--grammar", choices=("expressions", "statements"), default="expressions",
                        help="whether statements survive or everything becomes an expression")
    parser.add_argument("--wrapper", choices=("flat", "block", "fn"), default="block",
                        help="how a function body is delimited; ignored for --grammar expressions")
    parser.add_argument("--access", choices=("merge", "binary", "drop"), default="merge",
                        help="what `a.b` and `a::b` become")
    parser.add_argument("--implicit-self", dest="implicitSelf", choices=("drop", "keep"), default="drop",
                        help="what the leading `.` of `.member` becomes")
    parser.add_argument("--parentheses", choices=("drop", "keep"), default="drop",
                        help="whether grouping parentheses survive")
    parser.add_argument("--increments", choices=("synthesize", "keep"), default="synthesize",
                        help="turn `x += 1;` into `x++;` / `++x;`, which the input never uses")
    parser.add_argument("--strings", choices=("keep", "shorten"), default="keep",
                        help="whether string literals keep their contents")
    parser.add_argument("--fold", dest="operators", choices=("comma", "weighted", "fixed"),
                        default="comma", help="the operator spliced in for removed syntax")
    parser.add_argument("--seed", type=int, default=0, help="seed for the operator supply")
    parser.add_argument("--quiet", action="store_true")
    options = parser.parse_args(argv)

    warnings = []

    def report(message, token):
        warnings.append(f"line {token.line}: {message}")

    options.output.mkdir(parents=True, exist_ok=True)
    sources = {}
    for name in SOURCE_FILES:
        path = options.source / name
        if not path.exists():
            sys.exit(f"missing input file {path}")
        sources[name] = path.read_text()

    weights = dict.fromkeys(BINARY_PUNCTUATION, 0)
    for text in sources.values():
        for operator, count in binaryHistogram(lex(text)).items():
            weights[operator] += count
    operators = OperatorSource(options.operators, weights, options.seed)

    combined = []
    for name in SOURCE_FILES:
        del warnings[:]
        text, bodies = convert(sources[name], options, operators, report)
        for offender, count in check(text, options).items():
            warnings.append(f"{count} x {offender!r} left in the output")
        (options.output / name).write_text(text)
        combined.append(text)
        if not options.quiet:
            lines = text.count("\n")
            print(f"{name:<28} {bodies:>5} bodies  {len(text):>8} bytes  {lines:>6} lines"
                  + (f"  {len(warnings)} warnings" if warnings else ""))
        for warning in warnings[:5]:
            print(f"    {name}: {warning}", file=sys.stderr)

    whole = "".join(combined)
    (options.output / "benchmark-expr.chrg").write_text(whole)
    (options.output / "benchmark-expr-nocomments.chrg").write_text(tidy(stripComments(whole)))
    (options.output / "benchmark-expr-blockcomments.chrg").write_text(tidy(stripComments(whole, blocks=True)))
    if not options.quiet:
        print(f"{'benchmark-expr.chrg':<28} {len(whole):>8} bytes  {whole.count(chr(10)):>6} lines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
