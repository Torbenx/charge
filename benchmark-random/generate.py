#!/usr/bin/env python3
"""Derive the randomized benchmark corpus in `benchmark-random/` from `benchmark/`.

The output is a token sequence with no structure at all: every token is drawn
independently from the token frequencies of the input, so nothing about the
corpus is predictable beyond those frequencies.  What is preserved is the
statistical shape a lexer sees --

  * the average number of tokens per line,
  * the distribution that average comes from: how many lines are blank, how
    many hold nothing but a comment, and how the rest spread over token counts,
  * the mix of punctuation, keywords, identifiers and literals,
  * the identifiers and the numeric, string and character literals themselves,
    which are drawn from the input's vocabulary and keep its frequencies,
  * the indentation, and how often two adjacent token kinds are spaced apart,
    so that bytes per token stays where the input has it.

What is destroyed is every correlation between neighbouring tokens.  Real code
lets a lexer predict that `(` is usually followed by an identifier and that `;`
ends a line; here it cannot, which is the point of the corpus.

Because the tokens are independent, adjacent ones collide in ways real code
never produces -- `/` next to `/` would open a comment, `1` next to `.` would
lex as a single number -- so the renderer separates any pair that would not lex
back as two tokens, and every file is re-lexed to confirm that it holds exactly
the sequence that was generated.

Run `./benchmark-random/generate.py --help` for the knobs.
"""

import argparse
import bisect
import collections
import importlib.util
import pathlib
import random
import sys


def loadExprGenerator():
    """Import `benchmark-expr/generate.py`, whose lexer and layout helpers are shared.

    It is loaded by path and under a name of its own rather than off `sys.path`,
    because it is called `generate.py` too and would otherwise be found by this
    module's own name.
    """
    sys.dont_write_bytecode = True          # no `__pycache__` next to a sibling script
    path = pathlib.Path(__file__).resolve().parent.parent / "benchmark-expr" / "generate.py"
    spec = importlib.util.spec_from_file_location("benchmarkExprGenerate", path)
    if spec is None or spec.loader is None:
        sys.exit(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


expr = loadExprGenerator()

CHARACTER = expr.CHARACTER
DECLARATION_WORDS = expr.DECLARATION_WORDS
KEYWORDS = expr.KEYWORDS
NUMBER = expr.NUMBER
PUNCT = expr.PUNCT
PUNCTUATION = expr.PUNCTUATION
SOURCE_FILES = expr.SOURCE_FILES
STRING = expr.STRING
WORD = expr.WORD
Token = expr.Token
lex = expr.lex
stripComments = expr.stripComments

# `fn`, `struct` and friends are ordinary words to the lexer, but they are
# reserved by the grammar, so they are drawn from the keyword pool rather than
# from the identifier vocabulary.
#
# `benchmark-expr/generate.py` only lists the words its reduced grammar has to
# recognise, so the rest of `compiler/parse/parse_gen.h` is added here. Which
# side of the split a word falls on does not change the corpus -- drawing a kind
# and then a word from its pool gives every word the frequency it has in the
# input either way -- but it does decide what `--identifiers uniform` flattens
# and which spacing a word is rendered with.
RESERVED = KEYWORDS | DECLARATION_WORDS | {
    "base", "context", "incomplete", "open", "virtual",
}

# The kind a keyword is drawn under.  It is kept apart from `WORD` so that
# `--identifiers uniform` cannot flatten the keyword frequencies along with the
# identifier ones.
KEYWORD = "keyword"

BLANK = "blank"
COMMENT = "comment"
CODE = "code"


# --------------------------------------------------------------- distributions


class Distribution:
    """A seeded draw from an empirical `value -> count` histogram."""

    def __init__(self, counts, generator, uniform=False):
        self.generator = generator
        self.values = [value for value, count in counts.items() if count > 0]
        weights = [1] * len(self.values) if uniform else [counts[v] for v in self.values]
        self.total = sum(weights)
        self.cumulative = []
        running = 0
        for weight in weights:
            running += weight
            self.cumulative.append(running)

    def __bool__(self):
        return self.total > 0

    def next(self):
        pick = self.generator.random() * self.total
        return self.values[bisect.bisect_right(self.cumulative, pick)]


# ---------------------------------------------------------------------- model


class FileShape:
    """The per-file line statistics, which set the size and shape of the output."""

    def __init__(self):
        self.lines = 0
        self.tokens = 0
        self.lineKinds = collections.Counter()      # BLANK / COMMENT / CODE -> lines
        self.tokenCounts = collections.Counter()    # tokens on a code line -> lines
        self.indents = collections.Counter()        # indent of a code line -> lines
        self.comments = []                          # comment-only lines, verbatim


class Model:
    """Everything the generator learned from the input corpus."""

    def __init__(self):
        self.pools = {kind: collections.Counter()
                      for kind in (KEYWORD, WORD, PUNCT, NUMBER, STRING, CHARACTER)}
        self.kinds = collections.Counter()          # token kind -> count
        self.spacing = collections.Counter()        # (left, right) -> spaced count
        self.adjacency = collections.Counter()      # (left, right) -> total count
        self.files = {}                             # name -> FileShape

    @property
    def tokens(self):
        return sum(self.kinds.values())

    @property
    def lines(self):
        return sum(shape.lines for shape in self.files.values())


def kindOf(token):
    """The pool a token was drawn from, which is its lexer kind split on keywords."""
    if token.kind == WORD:
        return KEYWORD if token.text in RESERVED else WORD
    return token.kind


def spacingKey(token):
    """The symbol a token contributes to the spacing table.

    Punctuation is keyed by its exact text, because `(` and `;` are spaced
    completely differently, while all identifiers behave the same way.
    """
    return token.text if token.kind == PUNCT else kindOf(token)


def observe(model, name, text):
    """Fold one input file into `model`."""
    tokens = lex(text)[:-1]
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()                             # the trailing newline, not a line

    shape = FileShape()
    shape.lines = len(lines)
    shape.tokens = len(tokens)
    model.files[name] = shape

    perLine = collections.Counter(token.line for token in tokens)
    for number, line in enumerate(lines, start=1):
        count = perLine.get(number, 0)
        if count:
            shape.lineKinds[CODE] += 1
            shape.tokenCounts[count] += 1
            shape.indents[len(line) - len(line.lstrip())] += 1
        elif not line.strip():
            shape.lineKinds[BLANK] += 1
        else:
            shape.lineKinds[COMMENT] += 1
            # `stripComments(blocks=True)` wraps these in `/* ... */` later on,
            # which a `*/` inside the text would cut short.
            if "*/" not in line:
                shape.comments.append(line.rstrip())

    for token in tokens:
        kind = kindOf(token)
        model.kinds[kind] += 1
        model.pools[kind][token.text] += 1

    # Spacing is only observed between two tokens on the same line; a line break
    # is layout, and the indentation model covers it.
    for left, right in zip(tokens, tokens[1:]):
        if right.line != left.line:
            continue
        key = (spacingKey(left), spacingKey(right))
        model.adjacency[key] += 1
        if right.trivia:
            model.spacing[key] += 1


def build(sources):
    model = Model()
    for name, text in sources.items():
        observe(model, name, text)
    return model


# ------------------------------------------------------------------- rendering

# A word or a number runs into anything that could continue it: another word, a
# digit, and the quote of a literal in case the lexer ever grows prefixed ones.
CONTINUES_WORD = (WORD, NUMBER, STRING, CHARACTER)


def munch(text):
    """The length of the longest punctuator `text` starts with."""
    for punctuation in PUNCTUATION:             # ordered by descending length
        if text.startswith(punctuation):
            return len(punctuation)
    return 0


def glues(left, right):
    """True if emitting `right` straight after `left` would not lex as two tokens.

    Independent tokens land next to each other in combinations real code never
    produces, and the lexer takes the longest match it can: `x` before `y` is
    one identifier, `<` before `<` is one `<<`, `1` before `.` is one number,
    and `/` before `/` is not a token at all but the start of a comment.

    Kinds decide this, not just the characters at the seam. `.` after a word is
    the commonest pair in charge and lexes fine, while the same `.` after a
    number is swallowed by it.
    """
    if left.kind in (WORD, NUMBER):
        return right.kind in CONTINUES_WORD or (left.kind == NUMBER and right.text[0] == ".")
    if left.kind != PUNCT:
        return False                            # a literal delimits itself
    if left.text[-1] == "/" and right.text[0] in "/*":
        return True                             # would open a comment
    if left.text[-1] == "." and right.kind == NUMBER:
        return True                             # `.` `5` could lex as one number
    # Two punctuators glue exactly when the longest match across the seam runs
    # past the end of the left one, so `<` `<` does and `;` `&` does not.
    return munch(left.text + right.text) > munch(left.text)


class Renderer:
    """Turns drawn tokens into lines, spaced the way the input spaces them."""

    def __init__(self, model, generator, mode):
        self.model = model
        self.generator = generator
        self.mode = mode

    def spaced(self, left, right):
        if glues(left, right):
            return True
        if self.mode == "single":
            return True
        key = (spacingKey(left), spacingKey(right))
        total = self.model.adjacency.get(key)
        if not total:
            # An adjacency the input never shows: fall back to the kind pair,
            # and to a space if even that was never seen.
            key = (kindOf(left), kindOf(right))
            total = self.model.adjacency.get(key)
            if not total:
                return True
        # Drawing rather than rounding to the majority keeps the expected number
        # of spaces, and so bytes per token, where the input has it.
        return self.generator.random() * total < self.model.spacing.get(key, 0)

    def line(self, indent, tokens):
        parts = [" " * indent]
        for index, token in enumerate(tokens):
            if index and self.spaced(tokens[index - 1], token):
                parts.append(" ")
            parts.append(token.text)
        return "".join(parts)


# ------------------------------------------------------------------- generator


class TokenSource:
    """Independent draws of one token from the corpus frequencies."""

    def __init__(self, model, generator, options):
        uniform = {
            WORD: options.identifiers == "uniform",
            NUMBER: options.literals == "uniform",
            STRING: options.literals == "uniform",
            CHARACTER: options.literals == "uniform",
        }
        self.pools = {kind: Distribution(counts, generator, uniform.get(kind, False))
                      for kind, counts in model.pools.items() if counts}
        self.kinds = Distribution({kind: count for kind, count in model.kinds.items()
                                   if kind in self.pools}, generator)

    def next(self):
        kind = self.kinds.next()
        text = self.pools[kind].next()
        # The lexer kind is what `check()` compares against, so a keyword goes
        # back to being a word here.
        lexerKind = WORD if kind == KEYWORD else kind
        return Token(lexerKind, text, "", 0)


def balance(counts, target, generator):
    """Nudge sampled line lengths until they add up to exactly `target` tokens.

    Sampling `n` lines independently lands within a fraction of a percent of the
    input's tokens per line, but not on it.  Spreading the remainder one token
    at a time over randomly chosen lines closes the gap without visibly moving
    the distribution those lines were drawn from.
    """
    delta = target - sum(counts)
    guard = 100 * len(counts)
    while delta and guard > 0:
        guard -= 1
        index = generator.randrange(len(counts))
        if delta > 0:
            counts[index] += 1
            delta -= 1
        elif counts[index] > 1:
            counts[index] -= 1
            delta += 1
    return delta


def generate(shape, source, renderer, generator, options):
    """Produce one output file with the line shape of one input file.

    Returns the text and the tokens it was built from, so that the result can be
    checked against them.
    """
    kinds = Distribution(shape.lineKinds, generator)
    lengths = Distribution(shape.tokenCounts, generator)
    indents = Distribution(shape.indents, generator)
    comments = shape.comments if options.comments == "sample" else []

    plan = [kinds.next() for _ in range(shape.lines)]
    counts = [lengths.next() for kind in plan if kind == CODE]
    if options.balance == "exact" and counts:
        # Blank and comment lines carry no tokens, so the whole budget of the
        # file falls on the code lines that were drawn.
        balance(counts, shape.tokens, generator)

    lines = []
    written = []
    cursor = 0
    for kind in plan:
        if kind == BLANK or (kind == COMMENT and not comments):
            lines.append("")
        elif kind == COMMENT:
            lines.append(comments[generator.randrange(len(comments))])
        else:
            tokens = [source.next() for _ in range(counts[cursor])]
            cursor += 1
            written.extend(tokens)
            lines.append(renderer.line(indents.next(), tokens))
    return "\n".join(lines) + "\n", written


# -------------------------------------------------------------------- checking


def check(text, written):
    """Confirm the output lexes back into exactly the tokens that were written.

    Any disagreement means two drawn tokens ran together, or a drawn token was
    itself split, which would silently change the corpus a lexer is measured on.
    """
    actual = lex(text)[:-1]
    for index, (a, b) in enumerate(zip(actual, written)):
        if a.text != b.text or a.kind != b.kind:
            return (f"token {index}: lexed {a.text!r} ({a.kind}), "
                    f"wrote {b.text!r} ({b.kind})")
    if len(actual) != len(written):
        return f"lexed {len(actual)} tokens, wrote {len(written)}"
    return None


# ------------------------------------------------------------------------ main


def describe(name, text, tokens, lines, reference=None):
    report = (f"{name:<30} {len(text):>8} bytes  {lines:>6} lines"
              f"  {tokens / lines:>6.3f} tokens/line  {len(text) / tokens:>5.2f} bytes/token")
    if reference:
        wantTokens, wantLines, wantBytes = reference
        report += (f"  (input {wantTokens / wantLines:.3f} and"
                   f" {wantBytes / wantTokens:.2f})")
    return report


def main(argv=None):
    root = pathlib.Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=pathlib.Path, default=root.parent / "benchmark",
                        help="directory holding the corpus to learn from")
    parser.add_argument("--output", type=pathlib.Path, default=root,
                        help="directory to write the randomized corpus to")
    parser.add_argument("--identifiers", choices=("frequency", "uniform"), default="frequency",
                        help="draw identifiers with the frequencies of the input, or flat across "
                             "its vocabulary, which stresses identifier interning harder")
    parser.add_argument("--literals", choices=("frequency", "uniform"), default="frequency",
                        help="the same choice for numeric, string and character literals")
    parser.add_argument("--comments", choices=("sample", "none"), default="sample",
                        help="whether comment-only lines are drawn from the input's comments")
    parser.add_argument("--spacing", choices=("learned", "single"), default="learned",
                        help="space adjacent tokens the way the input does, or always by one space")
    parser.add_argument("--balance", choices=("exact", "none"), default="exact",
                        help="correct the sampled line lengths so that the tokens per line of each "
                             "file matches its input exactly instead of only in expectation")
    parser.add_argument("--seed", type=int, default=0, help="seed for every draw")
    parser.add_argument("--quiet", action="store_true")
    options = parser.parse_args(argv)

    sources = {}
    for name in SOURCE_FILES:
        path = options.source / name
        if not path.exists():
            sys.exit(f"missing input file {path}")
        sources[name] = path.read_text()

    model = build(sources)
    if not options.quiet:
        pools = ", ".join(f"{len(model.pools[kind])} {kind}"
                          for kind in (WORD, KEYWORD, PUNCT, NUMBER, STRING, CHARACTER))
        print(f"learned {model.tokens} tokens over {model.lines} lines: {pools}")

    generator = random.Random(options.seed)
    source = TokenSource(model, generator, options)
    renderer = Renderer(model, generator, options.spacing)

    options.output.mkdir(parents=True, exist_ok=True)
    failures = 0
    combined = []
    for name in SOURCE_FILES:
        shape = model.files[name]
        text, written = generate(shape, source, renderer, generator, options)
        problem = check(text, written)
        (options.output / name).write_text(text)
        combined.append(text)
        if not options.quiet:
            reference = (shape.tokens, shape.lines, len(sources[name]))
            print(describe(name, text, len(written), text.count("\n"), reference))
        if problem:
            failures += 1
            print(f"{name}: {problem}", file=sys.stderr)

    whole = "".join(combined)
    variants = {
        "benchmark-random.chrg": whole,
        "benchmark-random-nocomments.chrg": stripComments(whole),
        "benchmark-random-blockcomments.chrg": stripComments(whole, blocks=True),
    }
    for name, text in variants.items():
        (options.output / name).write_text(text)
        if not options.quiet:
            reference = (model.tokens, model.lines, sum(len(t) for t in sources.values()))
            print(describe(name, text, len(lex(text)) - 1, text.count("\n"),
                           reference if name == "benchmark-random.chrg" else None))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
