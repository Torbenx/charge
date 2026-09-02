# benchmark-expr

An expression-only benchmark corpus, derived mechanically from `benchmark/` by
`generate.py`.

The target grammar knows only

* prefix unary operators `! ~ + - ++ -- *`
* binary operators `, + - * & ^ | / % << >> && || != == < <= > >=`
* postfix operators `++ --`
* assignments `= += -= *= &= ^= |= /= %= <<= >>= &&= ||=`
* `;` between expressions
* identifiers and numeric, string and character literals

There are no statements, no keywords, no brackets of any kind, and no `.`,
`::` or `:`. The generator re-lexes everything it writes and reports any token
that escaped that alphabet.

## What the generator does

Only function bodies survive; every declaration around them is deleted. All
comments of the input are carried over verbatim and in place, and the original
indentation is kept, so the nesting of the code the corpus came from is still
visible even though the blocks themselves are gone.

Constructs the reduced grammar cannot express fold into `,` -- which an
argument list already treats as a binary operator -- rather than disappearing,
so the operands stay:

| input                    | output                |
| ------------------------ | --------------------- |
| `f(a, b)`                | `f, a, b`             |
| `f()`                    | `f`                   |
| `a[i]`                   | `a, i`                |
| `vector{T}`              | `vector, T`           |
| `a.b`, `a::b`            | `a_b`                 |
| `.b` (implicit self)     | `b`                   |
| `f(name: value)`         | `f, value`            |
| `(a + b) * c`            | `a + b * c`           |
| `return e;`              | `e;`                  |
| `let x: T = e;`          | `x = e;`              |
| `if c: { … }`            | `c; …`                |
| `for x: &const in r: { … }` | `x, r; …`          |
| `while c: { … }`         | `c; …`                |
| `break;`, `continue;`    | (nothing)             |
| `if c => a else b`       | `c, a, b`             |
| `fn f(…) => e;`          | `e;`                  |
| `x += 1;`                | `x++;` / `++x;`       |

That last rewrite is there because the input corpus spells every step as
`+= 1` and never once uses `++` or `--`; without it the increment and
decrement operators of the reduced grammar would go completely untested. Pass
`--increments keep` for a strictly faithful derivation.

## Files

`generate.py` writes one file per input plus the concatenation
`benchmark-expr.chrg` and its `-nocomments` and `-blockcomments` variants, the
same set `benchmark/` provides.

The result is 1.24 MB over 40.5k lines and 128k tokens, against 1.72 MB,
50.4k lines and 306k tokens for `benchmark/`, with 7.6k distinct identifiers.

## Options

Run `./benchmark-expr/generate.py --help` for the full list. The interesting
ones are:

* `--grammar statements` keeps `if`/`while`/`for`/`return`/… and the compound
  blocks, reducing only the expressions inside them.
* `--fold weighted` splices in real binary operators instead of `,`, drawn
  from a seeded source weighted by the binary-operator frequency of the input,
  which reproduces the operator mix of real charge code to within 0.1%.
* `--access binary` keeps `a.b` as two operands (`a OP b`) instead of merging
  it into the identifier `a_b`.
* `--parentheses keep` keeps grouping parentheses, so the parse trees keep the
  shapes the original code had.
* `--wrapper block|fn` delimits each body (only with `--grammar statements`).
