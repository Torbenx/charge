
<!-- .slide: data-background-image="assets/title.png" data-background-size="contain" -->

---

## Parsers

* <!-- .element: class="fragment" --> Specific context: programming languages
* <!-- .element: class="fragment" --> Transforms source code into AST
* <!-- .element: class="fragment" --> Done in two steps, lexing then parsing
* <!-- .element: class="fragment" --> Used by: compilers, language servers, formatters, ...

---

## Motivation

* Parser and lexer for <!-- .element: class="fragment" --> [Torbenx/charge](https://github.com/Torbenx/charge)
* &#32; <!-- .element: class="fragment" --> [Carbon Compiler Design](https://www.youtube.com/watch?v=ZI198eFghJk) at C++Now 2023
  * Challange: Parse and lex at 10M lines/sec
  * Combined at 5M lines/sec
* &#32; <!-- .element: class="fragment" --> [Reported hit](https://www.youtube.com/watch?v=hN6KcAKfTN0) at NDC Toronto 2026 on Apple M4
  * Lex at 12M lines/sec
  * Parse+Lex at 7M lines/sec

---

<!-- .slide: data-auto-animate -->

<span class="token-code-1">
<code data-id="token-1">conference</code>&nbsp;
<code data-id="token-2">=</code>&nbsp;
<code data-id="token-3">"CppCon"</code>&nbsp;
<code data-id="token-4">+</code>&nbsp;
<code data-id="token-5">to_string</code>
<code data-id="token-6">(</code>
<code data-id="token-7">2026</code>
<code data-id="token-8">)</code>
</span>

--

<!-- .slide: data-auto-animate -->



<span class="token-code-2">
<code data-id="token-1">conference</code>
<code data-id="token-2">=</code>
<code data-id="token-3">"CppCon"</code>
<code data-id="token-4">+</code>
<code data-id="token-5">to_string</code>
<code data-id="token-6">(</code>
<code data-id="token-7">2026</code>
<code data-id="token-8">)</code>
</span>

--

<!-- .slide: data-auto-animate -->



<span class="ast-tree">
<code data-id="token-2" style="grid-area:1/33/auto/span 2">=</code>
<code data-id="token-1" style="grid-area:2/12/auto/span 2">conference</code>
<code data-id="token-4" style="grid-area:2/54/auto/span 2">+</code>
<code data-id="token-3" style="grid-area:3/36/auto/span 2">"CppCon"</code>
<code data-id="token-6" style="grid-area:3/72/auto/span 2">(</code>
<code data-id="token-8" style="grid-area:3/72/auto/span 2;opacity:0">)</code>
<code data-id="token-5" style="grid-area:4/60/auto/span 2">to_string</code>
<code data-id="token-7" style="grid-area:4/84/auto/span 2">2026</code>
<svg data-id="ast-edges" class="ast-edges" viewBox="0 0 100 100" preserveAspectRatio="none"><line x1="34.375" y1="12.5" x2="12.5" y2="37.5"/><line x1="34.375" y1="12.5" x2="56.25" y2="37.5"/><line x1="56.25" y1="37.5" x2="37.5" y2="62.5"/><line x1="56.25" y1="37.5" x2="75" y2="62.5"/><line x1="75" y1="62.5" x2="62.5" y2="87.5"/><line x1="75" y1="62.5" x2="87.5" y2="87.5"/></svg>
</span>

---

## Main Loop

```c++ [|1,12|2,3|4,10|5-8|11|]
for (;;) {
    sourcePosition = skipWhitespace(sourcePosition);
    Token tok = Token::Invalid;
    switch (sourcePosition[0]) {
    case ',':
        tok = Token::Comma;
        sourcePosition += 1;
        break;
    ...
    }
    output.emitToken(tok, /* source-information */);
}
```

---

<!-- .slide: data-auto-animate -->
## `+` Tokens

```c++ [|1,2|3-5|6-8|9-12|]
case '+':
    // Tokens: ++ += +
    if (sourcePosition[1] == '+') {
        tok = Token::PlusPlus;
        sourcePosition += 2;
    } else if (sourcePosition[1] == '=') {
        tok = Token::PlusEqual;
        sourcePosition += 2;
    } else {
        tok = Token::Plus;
        sourcePosition += 1;
    }
    break;
```
<!-- .element: data-id="code-block" style="width: 800px;" -->

--

<!-- .slide: data-auto-animate -->
## `+` Tokens

```c++ [0: |16]
switch (sourcePosition[0]) {
case '+':
    // Tokens: ++ += +
    if (sourcePosition[1] == '+') {
        tok = Token::PlusPlus;
        sourcePosition += 2;
    } else if (sourcePosition[1] == '=') {
        tok = Token::PlusEqual;
        sourcePosition += 2;
    } else {
        tok = Token::Plus;
        sourcePosition += 1;
    }
    break;
}
output.emitToken(tok, /* source-information */);
```
<!-- .element: data-id="code-block" style="width: 800px;" -->

---

## String Literals

```c++ [|1|2|3,4|]
case '"':
    tok = Token::StringLiteral;
    sourcePosition = skipStringLiteral(sourcePosition + 1);
    sourcePosition += 1;
    break;
```

<span class="fragment">Note: No processing / unescaping</span>

---

## `/` Tokens And Comments

```c++ [|2-6|7-10|11-13|14-17|]
case '/':
    if (sourcePosition[1] == '*') {
        sourcePosition = skipBlockComment(sourcePosition + 2);
        sourcePosition += 2;
        output.recordComment(CommentKind::Block, /* source-information */);
        continue;
    } else if (sourcePosition[1] == '/') {
        sourcePosition = skipSkipToEndOfLine(sourcePosition + 2);
        output.recordComment(CommentKind::Line, /* source-information */);
        continue;
    } else if (sourcePosition[1] == '=') {
        tok = Token::SlashEqual;
        sourcePosition += 2;
    } else {
        tok = Token::Slash;
        sourcePosition += 1;
    }
    break;
```

---

## Identifiers

* <!-- .element: class="fragment" --> Key decision: identifier table or not
* <!-- .element: class="fragment" --> Extremely useful for compilers
* <!-- .element: class="fragment" --> Very hash table heavy
  * [Matt Kulukundis at CppCon 2017](https://www.youtube.com/watch?v=ncHmEUmJZf4)
  * [Malte Skarupke at C++Now 2018](https://www.youtube.com/watch?v=M2fKMP47slQ)
* <!-- .element: class="fragment" --> Focus on formatters
  * Need keyword recognition only

--

## Identifiers

```c++ [|1-3|4,5|6,7|]
case 'A'..='Z':
case 'a'..='z':
case '_':
    const char* begin = sourcePosition;
    sourcePosition = skipIdentifier(sourcePosition + 1);
    auto* entry = KeywordTable::get(begin, sourcePosition);
    tok = entry ? entry->token : Token::Identifier;
    break;
```
<span class="fragment"><code>KeywordTable</code> generated by <code>gperf</code></span>

---

## Switch And Branch Summary

```c++
( ) [ ] { } ~ . , : ; + - * / % ^ ! & | < > =
:: ++ -- && || << >> ==
+= -= *= /= %= ^= != &= |= <= >=
&&= ||= <<= >>=
-> =>
// /*
```
* <!-- .element: class="fragment" --> Integer, string and character literals
* <!-- .element: class="fragment" --> Identifiers / keywords
* <!-- .element: class="fragment" --> New lines and null terminator
* <!-- .element: class="fragment" --> 30 switch cases and 58 total branches

---

<!-- .slide: data-auto-animate -->
## Benchmark Setup

```c++ [|1|3-11]
const char* lexSwitchAndBranch(const char* sourcePosition, LexerOutput& output);

void runGoogleBenchmark(benchmark::State& state, std::string file) {
    std::string source = readFile(file);

    for (auto _ : state) {
        LexerOutput output { source };
        const char* end = lexSwitchAndBranch(source.data(), output);
        assert(end == source.data() + source.size());
    }
}
```
<!-- .element: data-id="code-block" style="width: 1200px;" -->

--

<!-- .slide: data-auto-animate -->
## Benchmark Setup

```c++ [3-24|12-16|18-23]
const char* lexSwitchAndBranch(const char* sourcePosition, LexerOutput& output);

void runGoogleBenchmark(benchmark::State& state, std::string file) {
    std::string source = readFile(file);

    for (auto _ : state) {
        LexerOutput output { source };
        const char* end = lexSwitchAndBranch(source.data(), output);
        assert(end == source.data() + source.size());
    }

    LexerOutput output { source };
    lexSwitchAndBranch(source.data(), output);
    auto bytes = source.size();
    auto lines = output.lines.size();
    auto tokens = output.tokens.size();

    state.counters["bytes"] = benchmark::Counter(
        bytes, benchmark::Counter::kIsIterationInvariantRate);
    state.counters["lines"] = benchmark::Counter(
        lines, benchmark::Counter::kIsIterationInvariantRate);
    state.counters["tokens"] = benchmark::Counter(
        tokens, benchmark::Counter::kIsIterationInvariantRate);
}
```
<!-- .element: data-id="code-block" style="width: 1200px;" -->

---

## Switch And Branch Benchmark

<!-- ./build/charge gbench --benchmark_filter=benchmarkImpl/switch-and-branch -->

* <!-- .element: class="fragment" --> ~1000 lines of realistic input data
* <!-- .element: class="fragment" --> Reaches ~16M lines/sec
* <!-- .element: class="fragment" --> High run-to-run variance

---

## What To Optimize

* <!-- .element: class="fragment" --> <code>skipIdentifier</code> and friends &longrightarrow; SIMD
* <!-- .element: class="fragment" --> Identifier table &longrightarrow; hash table
* <!-- .element: class="fragment" --> Token recognition &longrightarrow; branch heavy
  * `token-begin` &longmapsto; `(token enum, advance)`

---

## How To Reduce Branches

* <!-- .element: class="fragment" --> Completely branchless inpractical
  * Many different `skip` functions
* <!-- .element: class="fragment" --> Reduce as much as possible
  * One case per `skip` function
  * Plus one case for punctuations
* <!-- .element: class="fragment" --> Need to recognise all of:
```c++
( ) [ ] { } ~ . , : ; + - * / % ^ ! & | < > =
:: ++ -- && || << >> ==
+= -= *= /= %= ^= != &= |= <= >=
&&= ||= <<= >>=
-> =>
// /*
```
<!-- .element: class="fragment" -->

---

## 3 Character Lookup Table

* <!-- .element: class="fragment" --> <code>256 * 256 * 256 * 1 byte per entry</code>
* <!-- .element: class="fragment" --> <code>= 16 MiB</code> in total
* <!-- .element: class="fragment" --> Larger than L3 cache

---

## Exploit Token Shapes

```c++
( ) [ ] { } ~ . , : ; + - * / % ^ ! & | < > =
:: ++ -- && || << >> ==
+= -= *= /= %= ^= != &= |= <= >=
&&= ||= <<= >>=
-> =>
// /*
```
<!-- .element: class="fragment" -->

* <!-- .element: class="fragment" --> Punctuations can be:
  * <!-- .element: class="fragment" --> A single character
  * <!-- .element: class="fragment" --> A character repeated twice
  * <!-- .element: class="fragment" --> A character followed by <code>'='</code>
  * <!-- .element: class="fragment" --> A character repeated twice followed by <code>'='</code>
  * <!-- .element: class="fragment" --> A character followed by <code>'>'</code>
  * <!-- .element: class="fragment" --> <code>/*</code>

---

<!-- .slide: data-auto-animate -->
## Implementation

```c++ [|1,2|4|5,6|7,8|10,11|13|14-17|18-21|22|23-25|27|]
for (;;) {
    sourcePosition = skipWhitespace(sourcePosition);

    bool repeat = sourcePosition[1] == sourcePosition[0];
    int equalTestOffset = repeat ? 2 : 1;
    bool equal = sourcePosition[equalTestOffset] == '=';
    char extraTestCharacter = sourcePosition[0] == '/' ? '*' : '>';
    bool extra = sourcePosition[1] == extraTestCharacter;

    auto [tok, advance] = lookup(sourcePosition[0],
                                 repeat, equal, extra);

    switch (tok) {
    case LINE_COMMENT_PLACEHOLDER:
        sourcePosition = skipToEndOfLine(sourcePosition + 2);
        output.recordComment(CommentKind::Line, /* source-information */);
        continue;
    case Token::StringLiteral:
        sourcePosition = skipStringLiteral(sourcePosition);
        sourcePosition += 1;
        break;
    ...
    default:
        sourcePosition += advance;
        break;
    }
    output.emitToken(tok, /* source-information */);
}
```
<!-- .element: data-id="code-block" style="width: 1100px;" -->

--

<!-- .slide: data-auto-animate -->
## Implementation

```c++ [|2,3|7-10|11|]
struct Entry {
    Token token : 6;
    uint8_t advance : 2;
};

Entry lookup(char c, bool repeat, bool equal, bool extra) {
    size_t index = (size_t)character
                   | ((size_t)repeat << 7)
                   | ((size_t)equal << 8)
                   | ((size_t)extra << 9);
    return table[index];
}
```
<!-- .element: style="width: 1100px;" -->

***

```c++ [4:]
    bool repeat = sourcePosition[1] == sourcePosition[0];
    int equalTestOffset = repeat ? 2 : 1;
    bool equal = sourcePosition[equalTestOffset] == '=';
    char extraTestCharacter = sourcePosition[0] == '/' ? '*' : '>';
    bool extra = sourcePosition[1] == extraTestCharacter;

    auto [tok, advance] = lookup(sourcePosition[0],
                                 repeat, equal, extra);
```
<!-- .element: data-id="code-block" style="width: 1100px;" -->

---

## Pattern Table Summary

<!-- ./build/charge gbench --benchmark_filter="benchmarkImpl/(switch-and-branch|pattern-table)" -->

* <!-- .element: class="fragment" --> 10 switch cases
* <!-- .element: class="fragment" --> Table size is <code>128 * 2 * 2 * 2 = 1024</code>
* <!-- .element: class="fragment" --> Lots of instructions
* <!-- .element: class="fragment" --> Dependence between instructions
* <!-- .element: class="fragment" --> Reaches ~12.6M lines/sec