# benchmark-random

A randomized benchmark corpus, derived mechanically from `benchmark/` by
`generate.py`.

Every token is drawn independently from the token frequencies of `benchmark/`,
so the corpus is a token soup: it lexes cleanly, and it means nothing. What
makes it useful is that it is the same size and shape as the corpus it came
from, so a lexer run against both differs only in how predictable the token
sequence was.

## What is preserved

* **Tokens per line**, exactly: 6.066 over the whole corpus, and per file the
  same figure the corresponding input file has. Blank and comment lines are
  part of that average, so they are reproduced too.
* **The distribution behind it**, not just the mean. 19.8% of lines carry no
  token at all and 16.5% carry exactly one; a Poisson draw with the right mean
  would get neither.
* **The token mix**: 55.6% punctuation, 40.5% words (of which 23.4% keywords),
  3.3% numeric, 0.44% string and 0.17% character literals.
* **The vocabulary**. Identifiers and literals are drawn from the input's own,
  with the input's frequencies, so the 5348 identifiers, 834 numbers, 933
  strings and 81 character literals a lexer interns are the real ones and are
  as skewed as they really are.
* **The layout**: indentation drawn from the input's indent histogram, comment
  lines carried over verbatim, and a space between two adjacent tokens with the
  probability the input spaces that particular pair.

## What is destroyed

Every correlation between neighbouring tokens. Real code lets a lexer predict
that `(` is usually followed by an identifier, that `;` ends a line and that a
`}` sits alone on one; here it cannot. That is the whole point of the corpus:
against `benchmark/` it isolates how much of a lexer's throughput comes from
the branch predictor having learned the shape of real charge.

## Collisions

Independent tokens land next to each other in combinations real code never
produces, and the lexer takes the longest match it can. `/` before `/` is not
two tokens but the start of a comment, `<` before `<` is one `<<`, and `1`
before `.` is a single number. `generate.py` separates exactly those pairs and
leaves the rest to the spacing model, then re-lexes every file it writes and
fails if the result is not token-for-token the sequence it generated.

This costs some density: the output runs at 5.82 bytes per token against 5.64
in `benchmark/`, 3.2% more. Almost none of that is the collision handling, which
forces a space on 20.8% of the pairs on a line; it is that drawing both sides of
a pair independently lands far more often on the combinations charge spaces --
`;` or `:` or `)` before a word -- than on the ones it does not, like `.` before
a word. The spacing of each individual pair is the input's own.

## Files

`generate.py` writes one file per input plus the concatenation
`benchmark-random.chrg` and its `-nocomments` and `-blockcomments` variants,
the same set `benchmark/` and `benchmark-expr/` provide.

The result is 1.78 MB over 50420 lines and 305842 tokens, against 1.72 MB, the
same 50420 lines and the same 305842 tokens for `benchmark/`.

Nothing here parses, so only the lexers are benchmarked against it:

```
./benchmark-random.bash 'switch-and-branch|table-hybrid'
```

## Options

Run `./benchmark-random/generate.py --help` for the full list.

* `--identifiers uniform` draws identifiers flat across the vocabulary instead
  of by frequency, which stresses identifier interning much harder than real
  code does. `--literals uniform` does the same for literals.
* `--spacing single` puts one space between every pair of tokens instead of
  reproducing the input's spacing.
* `--balance none` leaves the sampled line lengths alone, so tokens per line
  matches the input in expectation rather than exactly.
* `--comments none` drops comment lines, leaving them blank.
* `--seed` changes every draw; the corpus in the repository is seed 0.
