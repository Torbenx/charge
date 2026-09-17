#!/usr/bin/env python3
"""Charge (.chrg) -> Carbon (.carbon) converter for the benchmark corpus.

See benchmark-carbon/CARBON_CONVERSION.md for the mapping rationale.
"""
import re, sys, os

# ---------------------------------------------------------------- scanning

def mask(text):
    """Return a copy of `text` with string/char-literal bodies and comments
    replaced by NUL, so brackets inside them are invisible to the passes."""
    m = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i+1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            for k in range(i, j):
                m[k] = '\x00'
            i = j
        elif c == '"' or c == "'":
            q = c
            j = i + 1
            while j < n and text[j] != q:
                if text[j] == '\\':
                    j += 1
                j += 1
            j = min(j + 1, n)
            for k in range(i, j):
                if m[k] != '\n':
                    m[k] = '\x00'
            i = j
        else:
            i += 1
    return ''.join(m)


def apply_edits(text, edits):
    """edits: list of (start, end, replacement); non-overlapping."""
    edits = sorted(edits, key=lambda e: e[0])
    out, pos = [], 0
    for s, e, r in edits:
        assert s >= pos, (s, pos, r)
        out.append(text[pos:s]); out.append(r); pos = e
    out.append(text[pos:])
    return ''.join(out)


def sub_code(text, pattern, repl):
    """re.sub restricted to code (non-string, non-comment) regions."""
    mt = mask(text)
    edits = []
    for m in re.finditer(pattern, mt):
        edits.append((m.start(), m.end(), m.expand(repl) if isinstance(repl, str)
                      else repl(m)))
    return apply_edits(text, edits)


def match_fwd(mt, i):
    """Given index of an opening bracket, return index of its match."""
    pairs = {'(': ')', '[': ']', '{': '}'}
    close = pairs[mt[i]]
    depth = 0
    for j in range(i, len(mt)):
        c = mt[j]
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                assert c == close, (mt[i], c, j)
                return j
    raise ValueError('unbalanced at %d' % i)


# ------------------------------------------------- P0: operator overloads

OPS = {
    '+':  ('AddWith', 'Op', True),
    '-':  ('SubWith', 'Op', True),
    '*':  ('MulWith', 'Op', True),
    '/':  ('DivWith', 'Op', True),
    '%':  ('ModWith', 'Op', True),
    '&':  ('BitAndWith', 'Op', True),
    '|':  ('BitOrWith', 'Op', True),
    '^':  ('BitXorWith', 'Op', True),
    '<<': ('LeftShiftWith', 'Op', True),
    '>>': ('RightShiftWith', 'Op', True),
    '==': ('EqWith', 'Equal', False),
    '!=': ('EqWith', 'NotEqual', False),
    '<':  ('OrderedWith', 'Less', False),
    '<=': ('OrderedWith', 'LessOrEquivalent', False),
    '>':  ('OrderedWith', 'Greater', False),
    '>=': ('OrderedWith', 'GreaterOrEquivalent', False),
}

DECL_RE = re.compile(r'^fn impl \((?:a (?P<bin>\S+) b|(?P<un>-)a)\)'
                     r'\((?P<params>.*)\)(?: -> (?P<ret>[^:]+))?: \{$')


def split_params(s):
    out, depth, cur = [], 0, ''
    for c in s:
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(cur); cur = ''
        else:
            cur += c
    if cur.strip():
        out.append(cur)
    return [x.strip() for x in out]


def param_type(p):
    """'l: &const shared Vec2' -> 'Vec2';  'r: int' -> 'int'"""
    t = p.split(':', 1)[1].strip()
    t = re.sub(r'^&\s*(const\s+|unique\s+|shared\s+)*', '', t)
    return t.strip()


def operator_impls(text):
    """Q3 option B: expand `fn impl (op)(...)` into Carbon `impl ... as Core.X`
    blocks, merging consecutive declarations that target the same impl."""
    lines = text.split('\n')
    out, i, n = [], 0, len(lines)
    while i < n:
        m = DECL_RE.match(lines[i])
        if not m:
            out.append(lines[i]); i += 1
            continue

        # Collect a run of declarations that share one impl instantiation.
        group, key = [], None
        while i < n:
            m = DECL_RE.match(lines[i]) if i < n else None
            if not m:
                # allow blank lines between members of one group
                if group and lines[i].strip() == '' and i + 1 < n \
                        and DECL_RE.match(lines[i+1]):
                    i += 1
                    continue
                break
            op = m.group('bin') or '-a'
            params = split_params(m.group('params'))
            ret = (m.group('ret') or '').strip()
            ltype = param_type(params[0])
            rtype = param_type(params[1]) if len(params) > 1 else None
            iface, method, has_result = (OPS[op] if op != '-a'
                                         else ('Negate', 'Op', True))
            k = (ltype, iface, rtype)
            if key is None:
                key = k
            elif k != key:
                break
            # find the body (from the line after the decl to its closing brace)
            depth, j = 1, i + 1
            while j < n and depth:
                depth += lines[j].count('{') - lines[j].count('}')
                j += 1
            body = lines[i+1:j-1]
            group.append((params, ret, method, has_result, body))
            i = j

        ltype, iface, rtype = key
        has_result = group[0][3]
        head = 'impl %s as Core.%s' % (ltype, iface)
        if rtype is not None:
            head += '(%s)' % rtype
        if has_result:
            head += ' \x01WHERE\x01 \x01RESULT\x01 = %s' % group[0][1]
        out.append(head + ' {')
        for gi, (params, ret, method, _hr, body) in enumerate(group):
            if gi:
                out.append('')
            selfname = params[0].split(':', 1)[0].strip()
            sig = 'self: &const shared'
            othername = None
            if len(params) > 1:
                othername = params[1].split(':', 1)[0].strip()
                sig += ', other: ' + params[1].split(':', 1)[1].strip()
            out.append('  fn %s(%s)%s: {' %
                       (method, sig, (' -> ' + ret) if ret else ''))
            for b in body:
                b = rename_ident(b, selfname, 'self')
                if othername:
                    b = rename_ident(b, othername, 'other')
                out.append(('  ' + b) if b.strip() else b)
            out.append('  }')
        out.append('}')
    return '\n'.join(out)


def rename_ident(line, old, new):
    return sub_code(line, r'\b%s\b' % re.escape(old), new)


# ------------------------------------------------------- P1: keyword clash

CLASH = ['alias', 'base', 'class', 'constraint', 'form', 'partial', 'where']


def rename_clashes(text):
    return sub_code(text, r'\b(%s)\b' % '|'.join(CLASH), r'\1_')


# ------------------------------------------- P2/P3: static access, self

def static_access(text):
    return sub_code(text, r'::', '.')


def implicit_self(text):
    """A leading `.` is Charge's implicit `self`; a `.` after a value is an
    ordinary member access.  On a continuation line only `)`/`]` carry a value
    across the newline -- a `}` there closes a block, so what follows is a new
    statement starting with implicit self."""
    mt = mask(text)
    edits = []
    for m in re.finditer(r'\.(?=[A-Za-z_])', mt):
        i = m.start()
        if i and (mt[i-1].isalnum() or mt[i-1] in '_)]}'):
            continue                       # member access, same token
        j = i - 1
        while j >= 0 and mt[j] in ' \t\n':
            j -= 1
        if '\n' in mt[j+1:i] and j >= 0 and mt[j] in ')]':
            continue                       # member access continued on a new line
        edits.append((i, i, 'self'))
    return apply_edits(text, edits)


# ------------------------------------------------ P4: {T} -> (T) type args

def type_args(text):
    mt = mask(text)
    edits, stack = [], []
    for i, c in enumerate(mt):
        if c == '{':
            is_param = i > 0 and (mt[i-1].isalnum() or mt[i-1] == '_')
            stack.append(is_param)
        elif c == '}':
            if stack and stack.pop():
                edits.append((i, i + 1, ')'))
                # the opener is recorded when we pop; find it again
        if c == '{' and stack and stack[-1]:
            edits.append((i, i + 1, '('))
    return apply_edits(text, edits)


# --------------------------------------------- P5: designated call args

def declaration_paren_groups(mt):
    """Offsets of `(` that open a declaration parameter list."""
    skip = set()
    for m in re.finditer(r'\btemplate\s*\(', mt):
        skip.add(m.end() - 1)
    for m in re.finditer(r'\bfn\b', mt):
        j = m.end()
        while j < len(mt) and mt[j] in ' \t\n':
            j += 1
        if mt.startswith('impl', j):
            j += 4
            while j < len(mt) and mt[j] in ' \t\n':
                j += 1
            if j < len(mt) and mt[j] == '(':      # the (a + b) operator spec
                skip.add(j)
                j = match_fwd(mt, j) + 1
        # skip the (possibly qualified) function name
        while j < len(mt) and (mt[j].isalnum() or mt[j] in '_.'):
            j += 1
        if j < len(mt) and mt[j] == '(':
            skip.add(j)
    return skip


def designated_args(text):
    mt = mask(text)
    skip = declaration_paren_groups(mt)
    edits, stack = [], []
    for i, c in enumerate(mt):
        if c in '([{':
            stack.append(i)
        elif c in ')]}':
            if not stack:
                continue
            a = stack.pop()
            if c != ')' or a in skip:
                continue
            if not (a > 0 and (mt[a-1].isalnum() or mt[a-1] in '_)]')):
                continue                      # not a call
            if ':' not in mt[a+1:i]:
                continue
            # split top-level arguments
            args, depth, cur = [], 0, a + 1
            for k in range(a + 1, i):
                ch = mt[k]
                if ch in '([{':
                    depth += 1
                elif ch in ')]}':
                    depth -= 1
                elif ch == ',' and depth == 0:
                    args.append((cur, k)); cur = k + 1
            args.append((cur, i))
            names = []
            for s, e in args:
                mm = re.match(r'(\s*)([a-z_][A-Za-z_0-9]*)\s*:(?!:)', mt[s:e])
                if not mm:
                    names = None
                    break
                names.append((s, s + mm.end(), mm.group(1), mm.group(2)))
            if not names:
                continue
            edits.append((a, a + 1, '({'))
            for s, e, lead, nm in names:
                edits.append((s, e, '%s.%s =' % (lead, nm)))
            edits.append((i, i + 1, '})'))
    return apply_edits(text, edits)


# ------------------------------------------------------- P8a: statements

def find_terminator(mt, start, chars):
    depth = 0
    for j in range(start, len(mt)):
        c = mt[j]
        if c in '([{':
            depth += 1
        elif c in ')]}':
            if depth == 0 and c in chars:
                return j
            depth -= 1
        elif depth == 0 and c in chars:
            return j
    raise ValueError('no terminator from %d' % start)


def statements(text):
    text = sub_code(text, r'\belse:(\s*\{)', r'else\1')

    def wrap_to_brace(kw):
        """`kw COND: {`  ->  `kw (COND) {`"""
        nonlocal text
        mt = mask(text)
        edits = []
        for m in re.finditer(r'\b%s\b' % kw, mt):
            j = m.end()
            col = find_terminator(mt, j, ':')
            while not mt[col+1:col+40].lstrip().startswith('{'):
                col = find_terminator(mt, col + 1, ':')
            edits.append((j, j + 1, ' ('))
            edits.append((col, col + 1, ')'))
        text = apply_edits(text, edits)

    wrap_to_brace('if')
    wrap_to_brace('while')

    # for: `for PAT: &mod in EXPR: {`  ->  `for (PAT in EXPR) {`
    mt = mask(text)
    edits = []
    for m in re.finditer(r'\bfor\b', mt):
        j = m.end()
        col = find_terminator(mt, j, ':')
        while not mt[col+1:col+40].lstrip().startswith('{'):
            col = find_terminator(mt, col + 1, ':')
        head = text[j:col]
        pat, _, expr = head.partition(' in ')
        pat = re.sub(r'\s*:\s*&\s*(?:const|unique|shared)'
                     r'(?:\s+(?:const|unique|shared))*\s*$', '', pat)
        edits.append((j, col + 1, ' (%s in %s)' % (pat.strip(), expr.strip())))
    text = apply_edits(text, edits)

    # assert / prove / discard / destroy  ->  Assert(...) etc.
    mt = mask(text)
    edits = []
    for m in re.finditer(r'\b(assert|prove|discard|destroy)\b', mt):
        semi = find_terminator(mt, m.end(), ';')
        edits.append((m.start(), m.end(), m.group(1).capitalize()))
        edits.append((m.end(), m.end() + 1, '('))
        edits.append((semi, semi, ')'))
    text = apply_edits(text, edits)

    # `loop: {` last, so the synthesized `while (true)` is not re-processed
    return sub_code(text, r'\bloop:(\s*\{)', r'while (true)\1')


# ------------------------------------------------ P7: reference categories

def references(text):
    # self receivers
    text = sub_code(text, r'\bself:\s*&const\s+shared\b', 'self')
    text = sub_code(text, r'\bself:\s*&(unique|shared)\b', 'ref self')
    # local `let x: &shared = ...`
    text = sub_code(text,
                    r'\b(let|var)\s+([A-Za-z_]\w*)\s*:\s*&\s*'
                    r'(?:const|unique|shared)'
                    r'(?:\s+(?:const|unique|shared))*\s*(?==)',
                    r'\1 ref \2: auto ')
    # return types: `-> &mod[...] T` / `-> &mod T`
    def ret(m):
        const = 'const ' if 'const' in m.group(1) else ''
        return '-> ref ' + const
    text = sub_code(text, r'->\s*&((?:const|unique|shared)'
                          r'(?:\s+(?:const|unique|shared))*)\s*'
                          r'(?:\[[^\]]*\]\s*)?', ret)
    # parameters
    text = sub_code(text, r'\b([A-Za-z_]\w*)\s*:\s*&unique\s+const\s+',
                    r'ref \1: const ')
    text = sub_code(text, r'\b([A-Za-z_]\w*)\s*:\s*&(?:unique|shared)\s+',
                    r'ref \1: ')
    text = sub_code(text, r'\b([A-Za-z_]\w*)\s*:\s*&const\s+shared\s+', r'\1: ')
    text = sub_code(text, r'\b([A-Za-z_]\w*)\s*:\s*&const\s+', r'\1: ')
    return text


# ------------------------------------------------------ P6: declarations

def declarations(text):
    lines = text.split('\n')
    out = []
    scope = []              # stack of 'ns' | 'struct' | 'enum' | 'block'
    pending_template = None
    for raw in lines:
        line = raw
        next_kind = None    # kind for the first `{` opened on this line

        m = re.match(r'^(\s*)namespace (\w+):\s*\{(.*)$', line)
        if m:
            out.append('%snamespace %s;%s' % (m.group(1), m.group(2), m.group(3)))
            scope.append('ns')
            continue

        m = re.match(r'^template\((.*)\)$', line)
        if m:
            pending_template = m.group(1)
            continue

        if pending_template is not None:
            m = re.match(r'^(\s*)fn (\w+)\((.*)$', line)
            if m:
                out.append('%sfn %s[%s]' % (m.group(1), m.group(2),
                                            pending_template))
                line = '%s    (%s' % (m.group(1), m.group(3))
                pending_template = None
            elif not re.match(r'^\s*struct ', line):
                raise SystemExit('unhandled template target: %r' % line)

        m = re.match(r'^(\s*)struct (bool|char|str|type):\s*\{\s*\}$', line)
        if m:
            out.append('%s// %s is built in' % (m.group(1), m.group(2)))
            pending_template = None
            continue

        m = re.match(r'^(\s*)struct (\w+):\s*\{(.*)$', line)
        if m:
            if pending_template is not None:
                out.append('class %s(%s)' % (m.group(2), pending_template))
                line = '%s{%s' % (m.group(1), m.group(3))
                pending_template = None
            else:
                line = '%sclass %s {%s' % (m.group(1), m.group(2), m.group(3))
            next_kind = 'struct'
        else:
            m = re.match(r'^(\s*)enum (\w+):\s*\{(.*)$', line)
            if m:
                line = '%schoice %s {%s' % (m.group(1), m.group(2), m.group(3))
                next_kind = 'enum'

        if next_kind is None:
            m = re.match(r'^template\((.*?)\)\s+fn\s+(\w+)\((.*)$', line)
            if m:
                line = 'fn %s[%s](%s' % (m.group(2), m.group(1), m.group(3))

            m = re.match(r'^(\s*)static\s+var\s+(.*)$', line)
            if m:
                line = '%svar %s' % (m.group(1), m.group(2))
            else:
                m = re.match(r'^(\s*)static\s+(.*)$', line)
                if m:
                    line = '%slet %s' % (m.group(1), m.group(2))

            if scope and scope[-1] == 'enum':
                m = re.match(r'^(\s*)(\w+)\s*(?:=[^;]+)?;(\s*(?://.*)?)$', line)
                if m:
                    out.append('%s%s,%s' % (m.group(1), m.group(2), m.group(3)))
                    continue

            if scope and scope[-1] == 'struct':
                if re.match(r'^\s*(?:ref\s+)?[a-z_]\w*\s*:\s*[^;]*;'
                            r'(\s*(?://.*)?)$', line) \
                        and not re.match(r'^\s*(fn|class|choice|let|var|base)\b',
                                         line):
                    ind = line[:len(line) - len(line.lstrip())]
                    line = '%svar %s' % (ind, line.strip())

            # `fn ...: {`  ->  `fn ... {` (also wrapped signatures)
            line = re.sub(r':(\s*\{)', r'\1', line)

        # walk braces in order, dropping the namespace's closing brace
        mt = mask(line)
        drop = []
        opened_first = False
        for i, c in enumerate(mt):
            if c == '{':
                if next_kind is not None and not opened_first:
                    scope.append(next_kind); opened_first = True
                else:
                    scope.append('block')
            elif c == '}':
                kind = scope.pop() if scope else 'block'
                if kind == 'ns':
                    drop.append(i)
        if drop:
            line = ''.join(ch for i, ch in enumerate(line) if i not in set(drop))
        out.append(line)
    return '\n'.join(out)


# ------------------------------------------------------- P9/P10: operators

def operators(text):
    text = sub_code(text, r'&&', 'and')
    text = sub_code(text, r'\|\|', 'or')
    text = sub_code(text, r'!(?!=)', 'not ')
    text = sub_code(text, r'~', '^')
    return text


ESCAPES = {'\\f': '\\u{0C}', '\\b': '\\u{08}', '\\v': '\\u{0B}',
           '\\a': '\\u{07}'}


def escapes(text):
    mt = mask(text)
    edits = []
    for i in range(len(text) - 1):
        if mt[i] == '\x00' and text[i] == '\\' and text[i:i+2] in ESCAPES:
            # only inside literals, and only if the backslash itself starts
            # an escape (not the second half of `\\`)
            k, back = i - 1, 0
            while k >= 0 and text[k] == '\\':
                back += 1; k -= 1
            if back % 2 == 0:
                edits.append((i, i + 2, ESCAPES[text[i:i+2]]))
    return apply_edits(text, edits)



# ------------------------------------------- P11: Carbon parenthesization

# Charge follows C precedence.  Carbon leaves many operator pairs
# incomparable and demands explicit parentheses.  This pass re-parenthesises
# a binary chain -- preserving its C meaning -- but only when Carbon would
# reject the chain as written, so expressions that already parse are
# untouched.

BIN_OPS = ['<<', '>>', '*', '/', '%', '+', '-', '&', '|', '^']
C_LEVEL = {'*': 3, '/': 3, '%': 3, '+': 4, '-': 4, '<<': 5, '>>': 5,
           '&': 8, '^': 9, '|': 10}
GROUP = {'*': 'Mul', '/': 'Mul', '%': 'Mod', '+': 'Add', '-': 'Add',
         '<<': 'Shift', '>>': 'Shift', '&': 'And', '|': 'Or', '^': 'Xor'}
ASSOC = {'Mul', 'Add', 'And', 'Or', 'Xor'}      # left-associative in Carbon


def _needs_parens(parent_op, child_op, is_left):
    """Would Carbon reject `child` as a bare operand of `parent`?"""
    pg, cg = GROUP[parent_op], GROUP[child_op]
    if pg == cg:
        return not (is_left and pg in ASSOC)
    if pg == 'Add' and cg == 'Mul':
        return False                      # Multiplicative binds tighter
    return True                           # every other pair is incomparable


def _scan_primary(mt, i):
    """Index just past the primary starting at i, or None."""
    n = len(mt)
    while i < n and mt[i] in ' \t\n':
        i += 1
    while i < n and (mt[i] in '-^*' or mt.startswith('not ', i)):
        i += 4 if mt.startswith('not ', i) else 1
        while i < n and mt[i] in ' \t\n':
            i += 1
    if i >= n:
        return None
    if mt[i] == '(':
        i = match_fwd(mt, i) + 1
    elif mt[i].isalnum() or mt[i] == '_' or mt[i] == '.':
        while i < n and (mt[i].isalnum() or mt[i] in '_.'):
            i += 1
    else:
        return None
    while i < n and mt[i] in '([':          # calls and indexing
        i = match_fwd(mt, i) + 1
    return i


def _next_op(mt, i):
    n = len(mt)
    while i < n and mt[i] in ' \t\n':
        i += 1
    for op in BIN_OPS:
        if mt.startswith(op, i):
            nxt = mt[i+len(op):i+len(op)+1]
            if nxt == '=' or (op in '<>' and nxt in '<>='):
                return None, i
            if op in ('&', '|') and nxt == op:
                return None, i
            return op, i
    return None, i


def _build(terms, ops):
    """Fold a flat chain into a tree using C precedence (left-associative)."""
    nodes = list(terms)
    ops = list(ops)
    for level in sorted(set(C_LEVEL[o] for o in ops)):
        i = 0
        while i < len(ops):
            if C_LEVEL[ops[i]] == level:
                nodes[i:i+2] = [(ops[i], nodes[i], nodes[i+1])]
                del ops[i]
            else:
                i += 1
    return nodes[0]


def _render(node, parent_op=None, is_left=False):
    if not isinstance(node, tuple):
        return node
    op, l, r = node
    out = '%s %s %s' % (_render(l, op, True), op, _render(r, op, False))
    if parent_op is not None and _needs_parens(parent_op, op, is_left):
        return '(' + out + ')'
    return out


def _valid(node, parent_op=None, is_left=False):
    if not isinstance(node, tuple):
        return True
    op, l, r = node
    if parent_op is not None and _needs_parens(parent_op, op, is_left):
        return False
    return _valid(l, op, True) and _valid(r, op, False)


def parenthesize(text):
    mt = mask(text)
    edits, i, n = [], 0, len(mt)
    while i < n:
        start = _scan_primary(mt, i)
        if start is None:
            i += 1
            continue
        # note: `i` only ever advances by one when no chain is rewritten, so
        # chains nested inside call arguments and brackets are reached too
        # a chain must begin at a token boundary
        j = i
        while j < n and mt[j] in ' \t\n':
            j += 1
        if j and (mt[j-1].isalnum() or mt[j-1] in '_.'):
            i += 1
            continue
        terms, ops, pos = [], [], start
        first = text[j:start]
        while True:
            op, opos = _next_op(mt, pos)
            if op is None:
                break
            nxt = _scan_primary(mt, opos + len(op))
            if nxt is None:
                break
            terms.append(text[(j if not terms else prev_pos):pos].strip()
                         if not terms else text[prev_pos:pos].strip())
            ops.append(op)
            prev_pos = opos + len(op)
            pos = nxt
        if ops:
            terms.append(text[prev_pos:pos].strip())
            tree = _build(terms, ops)
            if not _valid(tree):
                edits.append((j, pos, _render(tree)))
                i = pos
                continue
        i += 1
    return apply_edits(text, edits)


# ------------------------------------------------------------------ driver

def convert(text):
    text = operator_impls(text)
    text = rename_clashes(text)
    text = static_access(text)
    text = implicit_self(text)
    text = type_args(text)
    text = designated_args(text)
    text = statements(text)
    text = references(text)
    text = declarations(text)
    text = operators(text)
    text = parenthesize(text)
    text = escapes(text)
    text = text.replace('\x01WHERE\x01', 'where')
    text = text.replace('\x01RESULT\x01', '.Result')
    return text


if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    open(dst, 'w').write(convert(open(src).read()))
