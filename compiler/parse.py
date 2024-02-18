import string
import pathlib
import dataclasses

punctuationTokens = [
    "(", ")", "[", "]", "{", "}",
    "!", "~", "++", "--", "+", "-", "*",
    "&", "^", "|", "/", "%", "<<", ">>", "&&", "||", "!=", "==", "<", "<=", ">", ">=",
    "=", "+=", "-=", "*=", "&=", "^=", "|=", "/=", "%=", "<<=", ">>=", "&&=", "||=",
    ",", ".", ":", "::", ";", "=>", "<=>", "->",
]

keywords = [
    "if", "elif", "else", "match", "for", "while", "do",
    "return", "break", "continue", "loop", "guard", "try", "catch",
    "with", "analysis", "assert",
    "namespace", "struct", "trait", "object", "fn", "static",
    "template",
    "var", "let", "in", "inout", "out", "forward", "assign"
]

punctuations = punctuationTokens + ["//", "/*"]
punctuationAlphabet = "".join(sorted({p[0] for p in punctuations}))

def punctuationCppName(punc):
    characterNames = {
        '(': "LeftParen", ')': "RightParen", '[': "LeftSqure", ']': "RightSqure", '{': "LeftBrace", '}': "RightBrace",
        '!': "Exclaim", '~': "Tilde", '+': "Plus", '-': "Minus", '*': "Star",
        '&': "Amp", '^': "Hat", '|': "Vert", '/': "Slash", '%': "Percent", '<': "Less", '>': "Greater",
        '=': "Equal", ',': "Comma", '.': "Point", ';': "SemiColon", ':': "Colon"
    }
    return "".join([characterNames[c] for c in punc])

def keywordCppName(keyword):
    return keyword[0].upper() + keyword[1:]

def stateCppName(name):
    parts = name.split('_')
    parts = [p[0].upper() + p[1:] for p in parts]
    return "".join(parts)

def identifierCppName():
    return "Identifier"

indentationStep = ' ' * 4

currentDir = pathlib.Path(__file__).parent.resolve()

with open(currentDir / "parse.txt", "r") as f:
    lines = f.readlines()

class Case:
    def __init__(self, instructions):
        self.instructions = instructions

    def __repr__(self):
        return type(self).__name__ + " " + str(self.instructions)

class PunctuationCase(Case):
    def __init__(self, punctuation, instructions):
        super().__init__(instructions)
        self.punctuation = punctuation
    def cppName(self):
        return punctuationCppName(self.punctuation)

class KeywordCase(Case):
    def __init__(self, keyword, instructions):
        super().__init__(instructions)
        self.keyword = keyword
    def cppName(self):
        return keywordCppName(self.keyword)

class IdentifierCase(Case):
    def __init__(self, instructions):
        super().__init__(instructions)
    def cppName(self):
        return punctuationCppName(identifierCppName())

class ThenCase(Case):
    def __init__(self, instructions):
        super().__init__(instructions)

class EndCase(Case):
    def __init__(self, instructions):
        super().__init__(instructions)

@dataclasses.dataclass
class NextInstruction:
    newState: str
    carriesEmitNode: bool = False
    def format(self):
        return "next " + self.newState

@dataclasses.dataclass
class ThenInstruction:
    newState: str
    def format(self):
        return "then " + self.newState

@dataclasses.dataclass
class EmitNodeInstruction:
    nodeKindExpr: str
    tokenExpr: str | None = None
    delayed: bool = False
    def format(self):
        ret = "emitNode " + self.nodeKindExpr
        if not self.tokenExpr is None:
            ret += ", " + self.tokenExpr
        return ret

@dataclasses.dataclass
class UpdateKindInstruction:
    nodeKindExpr: str
    def format(self):
        return "updateKind " + self.nodeKindExpr

@dataclasses.dataclass
class PushScopeInstruction:
    scopeKindExpr: str
    def format(self):
        return "pushScope " + self.scopeKindExpr

@dataclasses.dataclass
class PopScopeInstruction:
    scopeKindExprs: list[str]
    def format(self):
        ret = "popScope " + self.scopeKindExprs[0]
        for expr in self.scopeKindExprs[1:]:
            ret += ", " + expr
        return ret

@dataclasses.dataclass
class AssignInstruction:
    leftName: str
    rightExpr: str
    def format(self):
        return self.leftName + " = " + self.rightExpr

@dataclasses.dataclass
class IfScopeInstruction:
    scopeKindExprs: list[str]
    instructions: list

    def format(self):
        ret = "ifScope " + self.scopeKindExprs[0]
        for expr in self.scopeKindExprs[1:]:
            ret += ", " + expr
        return ret

@dataclasses.dataclass
class ErrorInstruction:
    def format(self):
        return "error"

@dataclasses.dataclass
class State:
    kind: str
    name: str
    thenState: str
    parameters: list[str]
    cases: list[Case]

    def punctuationCase(self, punc: str) -> PunctuationCase | None:
        for c in self.cases:
            if type(c) is PunctuationCase and c.punctuation == punc:
                return c
        return None

    def punctuationCases(self) -> [PunctuationCase]:
        return [c for c in self.cases if type(c) is PunctuationCase]

    def keywordCase(self, keyword: str) -> KeywordCase | None:
        for c in self.cases:
            if type(c) is KeywordCase and c.keyword == keyword:
                return c
        return None

    def keywordCases(self) -> [KeywordCase]:
        return [c for c in self.cases if type(c) is KeywordCase]

    def identifierCase(self) -> IdentifierCase | None:
        for c in self.cases:
            if type(c) is IdentifierCase:
                return c
        return None

    def thenCase(self) -> ThenCase | None:
        for c in self.cases:
            if type(c) is ThenCase:
                return c
        return None

    def endCase(self) -> EndCase | None:
        for c in self.cases:
            if type(c) is EndCase:
                return c
        return None

class Parser:
    def __init__(self, lines):
        self.lines = lines
        self.line = ""
        self.lineNumber = 0
        self.advanceLine()

    def lineEmpty(self):
        rem = self.line.lstrip(" ")
        return len(rem) == 0 or rem[0] == '#' or rem[0] == '\r' or rem[0] == '\n'

    def lineLevel(self):
        level = 0
        copy = self.line
        while copy.startswith(indentationStep):
            level += 1
            copy = copy[len(indentationStep):]
        return level

    def atEnd(self):
        return len(self.lines) == 0 and self.lineEmpty()

    def advanceLine(self):
        if not self.lineEmpty():
            self.parseError("expected empty line")
        while not self.atEnd():
            self.lineNumber += 1
            self.line = self.lines[0]
            self.lines = self.lines[1:]
            if not self.lineEmpty():
                break

    def parseWord(self, alphabet = string.ascii_lowercase + string.ascii_uppercase + "_"):
        while len(self.line) != 0 and self.line[0] == ' ':
            self.line = self.line[1:]
        remaining = self.line.lstrip(alphabet)
        word = self.line[:len(self.line) - len(remaining)]
        if len(word) == 0:
            self.parseError("expected word")
        self.line = remaining
        return word

    def parseExpr(self):
        return self.parseWord(string.ascii_lowercase + string.ascii_uppercase + "_:.")

    def parsePunctuation(self):
        return self.parseWord(punctuationAlphabet)

    def parseError(self, s):
        raise Exception(s + " at line " + str(self.lineNumber))

    def parseStates(self):
        states = []
        while not self.atEnd():
            if self.lineLevel() != 0:
                self.parseError("expected level 0")
            stateKind = self.parseWord()
            if stateKind != "SwitchState" and stateKind != "LinearState":
                self.parseError("invalid state")
            name = self.parseWord()

            if self.line[0] != '(':
                self.parseError("expected '('")
            self.line = self.line[1:]

            parameters = []
            while self.line[0] != ')':
                param = self.parseWord()
                if self.line[0] == ',':
                    self.line = self.line[1:]
                parameters.append(param)
            self.line = self.line[1:]

            thenWord = self.parseWord()
            if thenWord != "then":
                self.parseError("expected 'then'")
            thenState = self.parseWord()
            self.advanceLine()
            cases = self.parseCases()
            states.append(State(stateKind, name, thenState, parameters, cases))
        return states

    def parseCases(self):
        cases = []
        while self.lineLevel() == 1:
            caseKind = self.parseWord()
            if caseKind == "punctuation":
                punc = self.parsePunctuation()
                self.advanceLine()
                instructions = self.parseInstructions()
                cases.append(PunctuationCase(punc, instructions))
            elif caseKind == "keyword":
                keyword = self.parseWord()
                self.advanceLine()
                instructions = self.parseInstructions()
                cases.append(KeywordCase(keyword, instructions))
            elif caseKind == "identifier":
                self.advanceLine()
                instructions = self.parseInstructions()
                cases.append(IdentifierCase(instructions))
            elif caseKind == "then":
                self.advanceLine()
                instructions = self.parseInstructions()
                cases.append(ThenCase(instructions))
            elif caseKind == "end":
                self.advanceLine()
                instructions = self.parseInstructions()
                cases.append(EndCase(instructions))
            else:
                self.parseError("invalid case '" + caseKind + "'")
        return cases

    def parseInstructions(self, level = 2):
        instructions = []
        while self.lineLevel() == level:
            first = self.parseWord()
            if first == "next":
                delayed = False
                if len(instructions) > 0 and type(instructions[-1]) == EmitNodeInstruction and instructions[-1].tokenExpr is None:
                    instructions[-1].delayed = True
                    delayed = True
                instructions.append(NextInstruction(self.parseWord(), delayed))
                self.advanceLine()
            elif first == "emitNode":
                nodeKindExpr = self.parseExpr()
                while self.line[0] == ' ':
                    self.line = self.line[1:]
                tokenExpr = None
                if self.line[0] == ',':
                    self.line = self.line[1:]
                    tokenExpr = self.parseExpr()
                instructions.append(EmitNodeInstruction(nodeKindExpr, tokenExpr))
                self.advanceLine()
            elif first == "updateKind":
                instructions.append(UpdateKindInstruction(self.parseExpr()))
                self.advanceLine()
            elif first == "pushScope":
                instructions.append(PushScopeInstruction(self.parseExpr()))
                self.advanceLine()
            elif first == "popScope":
                scopes = []
                while not self.lineEmpty():
                    scopes.append(self.parseExpr())
                    if self.line[0] == ',':
                        self.line = self.line[1:]
                instructions.append(PopScopeInstruction(scopes))
                self.advanceLine()
            elif first == "ifScope":
                scopes = []
                while not self.lineEmpty():
                    scopes.append(self.parseExpr())
                    if self.line[0] == ',':
                        self.line = self.line[1:]
                self.advanceLine()
                instructions.append(IfScopeInstruction(scopes, self.parseInstructions(level + 1)))
            elif first == "then":
                instructions.append(ThenInstruction(self.parseWord()))
                self.advanceLine()
            else:
                while self.line[0] == ' ':
                    self.line = self.line[1:]
                if self.line[0] != '=':
                    self.parseError("invalid instruction")
                self.line = self.line[1:]
                instructions.append(AssignInstruction(first, self.parseExpr()))
                self.advanceLine()

        return instructions

states = Parser(lines).parseStates()

outputIndentation = 1
generatedLines = []

def line(line: str = ""):
    generatedLines.append('    ' * outputIndentation + line)

def labelLine(line: str):
    generatedLines.append('    ' * (outputIndentation - 1) + line)

def lineNoIndent(line: str = ""):
    generatedLines.append(line)

def indent():
    return IndentHelper()

class IndentHelper:
    def __enter__(self):
        global outputIndentation
        outputIndentation += 1

    def __exit__(self, *args):
        global outputIndentation
        outputIndentation -= 1

def linearIf(commonPrefix: str, state):
    puncs = sorted(filter(lambda p: (p.startswith(commonPrefix)), punctuations))
    possibleContinuations = set()
    exactMatch: str | None = None
    for p in puncs:
        if len(p) == len(commonPrefix):
            assert(exactMatch == None)
            exactMatch = p
        else:
            possibleContinuations.add(p[len(commonPrefix)])
    assert(exactMatch != None)

    if possibleContinuations:
        line("char next = tokEnd[" + str(len(commonPrefix)) + "];")
    for character in sorted(possibleContinuations):
        line("if (next == '" + character + "') {")
        with indent():
            linearIf(commonPrefix + character, state)
        line("}")

    line("tokEnd += " + str(len(exactMatch)) + ";")
    if exactMatch == "//":
        if onlyUsedAsThen(state):
            line("VERIFY_NOT_REACHED();")
        else:
            line("tokEnd = skipToEndOfLine(tokEnd);")
            line("emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, state);")
            line("goto " + state.name + "$no_emit;")
    elif exactMatch == "/*":
        if onlyUsedAsThen(state):
            line("VERIFY_NOT_REACHED();")
        else:
            line("tokEnd = skipToEndOfBlockComment(tokEnd);")
            line("tokEnd += 2;")
            line("emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, state);")
            line("goto " + state.name + "$no_emit;")
    else:
        foundState, case = recurse(state, lambda s: s.punctuationCase(exactMatch))
        generateCaseBody(case)

def checkForPunctuation(punc, handler):
    puncs = sorted(filter(lambda p: (p.startswith(punc)), punctuations))
    assert(len(puncs) > 0)
    line("if (std::string_view(tokEnd, " + str(len(punc)) + ") == \"" + punc + "\"sv) {")
    with indent():
        if len(puncs) == 1:
            assert(puncs[0] == punc)
            line("tokEnd += " + str(len(punc)) + ";")
            handler()
        else:
            possibleContinuations = set()
            for p in puncs:
                if p != punc:
                    possibleContinuations.add(p[len(punc)])
            line("char next = tokEnd[" + str(len(punc)) + "];")
            condition = ""
            for c in possibleContinuations:
                condition += " && next != '" + c + "'"
            condition = condition[4:]
            line("if (" + condition + ") {")
            with indent():
                line("tokEnd += " + str(len(punc)) + ";")
                handler()
            line("}")
    line("}")

def inlineTokenAdvancer():
    line("tokEnd = inlineAdvancer(tokEnd, state);")
    line("tokBegin = tokEnd;")

def emitNode(nodeKindExpr, beginExpr, endExpr):
    line("emitNode(" + nodeKindExpr + ", " + beginExpr +", nodeData, state);")
    line("nodeData = 0;")

def emitCarriedNode():
    emitNode("carriedEmitNodeKind", "tokBegin", "tokEnd")

def rememberState(state):
    #line("fmt::println(\"" + state.name + ": {}\", *tokEnd);")
    line("parseState = State::" + stateCppName(state.name) + ";")

def readWord():
    line("{")
    with indent():
        line("auto wordAndPos = readWord(tokEnd, state.wordTable);")
        line("tokEnd = wordAndPos.position;")
        line("word = wordAndPos.word;")
    line("}")

def generateWordCase(state):
    if state.name == "error":
        # TODO: Map keywords to keyword tokens
        labelLine("error$keyword_check:")
        labelLine("error$identifier_case:")
        line("VERIFY_NOT_REACHED();")
    else:
        readWord()
        line("if (word.keyword()) {")
        with indent():
            labelLine("[[maybe_unused]] " + state.name + "$keyword_check:")
            for c in state.keywordCases():
                line("if (word == words[\"" + c.keyword + "\"]) {")
                with indent():
                    generateCaseBody(c)
                line("}")
            if not state.thenCase() is None:
                generateCaseBody(state.thenCase(), "keyword_check")
            line("goto " + state.thenState + "$keyword_check;")
        line("}")
        line("nodeData = word.asUint();")
        labelLine("[[maybe_unused]] " + state.name + "$identifier_case:")
        if not state.identifierCase() is None:
            generateCaseBody(state.identifierCase())
        else:
            if not state.thenCase() is None:
                generateCaseBody(state.thenCase(), "identifier_case")
            line("goto " + state.thenState + "$identifier_case;")

errorCases  = [PunctuationCase(p, [ErrorInstruction()]) for p in punctuations]
errorCases += [IdentifierCase([ErrorInstruction()])]
errorState = State("SwitchState", "error", "", [], errorCases)
states += [errorState]

def findState(name: str) -> State:
    global states
    for s in states:
        if s.name == name:
            return s
    raise Exception("state '" + name + "' undefined")

def recurse(state, f):
    while True:
        val = f(state)
        if not val is None:
            return state, val
        state = findState(state.thenState)

def generateState(state):
    if state.kind == "SwitchState":
        generateSwitchState(state)
    elif state.kind == "LinearState":
        generateLinearState(state)
    else:
        assert(False)

def generateSwitchState(state):
    line("switch (tokEnd[0]) {")

    # newline
    if onlyUsedAsThen(state):
        line("case '\\n':")
        line("case '\\r':")
        with indent():
            line("VERIFY_NOT_REACHED();")
    else:
        line("case '\\n': {")
        with indent():
            line("tokEnd += 1;")
            line("markLineBegin(tokEnd, state);")
            line("goto " + state.name + "$no_emit;")
        line("}")

        line("case '\\r': {")
        with indent():
            line("if (tokEnd[1] == '\\n') {")
            with indent():
                line("tokEnd += 2;")
            line("} else {")
            with indent():
                line("tokEnd += 1;")
            line("}")
            line("markLineBegin(tokEnd, state);")
            line("goto " + state.name + "$no_emit;")
        line("}")

    # punctuations
    firstCharacters = {p[0] for p in punctuations}
    for character in sorted(firstCharacters):
        line("case '" + character + "': {")
        with indent():
            linearIf(str(character), state)
        line("}")

    # word
    for character in string.ascii_lowercase + string.ascii_uppercase:
        line("case '" + character + "':")
    line("case '#':")
    line("case '$':")
    line("case '_':")
    with indent():
        line("goto " + state.name + "$word_case;")

    # default
    line("default: {")
    with indent():
        line("VERIFY_NOT_REACHED();")
    line("}")

    line("} // switch")

    line("VERIFY_NOT_REACHED();")

    labelLine(state.name + "$word_case:")
    generateWordCase(state)

def generateLinearState(state):
    for c in state.punctuationCases():
        checkForPunctuation(c.punctuation, lambda: generateCaseBody(c))

    #if state.keywordCases() or not state.identifierCase() is None:
    line("if (isWordFirstCharacter(tokEnd[0])) {")
    with indent():
        generateWordCase(state)
    line("}")

    endCase = state.endCase()
    if not endCase is None:
        line("if (tokEnd[0] == '\\0') {")
        with indent():
            generateCaseBody(endCase)
            line("return;")
        line("}")

    thenCase = state.thenCase()
    if not thenCase is None:
        generateCaseBody(thenCase)
    line("// then " + state.thenState)
    line("goto " + state.thenState + "$as_then;")

def generateError(case):
    if type(case) is ThenCase:
        line("goto error$as_then;")
    else:
        line("errorToken = Token::" + case.cppName() + ";")
        line("goto handle_parse_error;")

def generateCaseBody(case, thenLabel = "as_then"):
    generateInstructions(case, case.instructions, thenLabel)

def generateInstructions(case, instructions, thenLabel):
    for inst in instructions:
        line("// " + inst.format())
        if type(inst) is EmitNodeInstruction:
            if inst.delayed:
                assert(inst.tokenExpr is None)
                line("carriedEmitNodeKind = "+ inst.nodeKindExpr + ";")
            elif inst.tokenExpr is None:
                emitNode(inst.nodeKindExpr, "tokBegin", "tokEnd")
            else:
                emitNode(inst.nodeKindExpr, inst.tokenExpr + "Begin", inst.tokenExpr + "End")
        elif type(inst) is UpdateKindInstruction:
            line("state.nodes.back().setKind(" + inst.nodeKindExpr + ");")
        elif type(inst) is NextInstruction:
            newState = findState(inst.newState)
            if shouldBeInlined(newState):
                line("// inlined " + newState.name)
                assert(newState.origins == [inst])
                assert(newState.kind == "LinearState")
                if inst.carriesEmitNode:
                    emitCarriedNode()
                inlineTokenAdvancer()
                rememberState(newState)
                generateState(newState)
            else:
                line("goto " + newState.name + ("$with_emit" if inst.carriesEmitNode else "$no_emit") + ";")
        elif type(inst) is AssignInstruction:
            if inst.leftName == "nodeKind":
                line("nodeKind = " + inst.rightExpr + ";")
            else:
                assert(False)
        elif type(inst) is PushScopeInstruction:
            line("scopePosition = pushScope(scopePosition, " + inst.scopeKindExpr + ");")
        elif type(inst) is PopScopeInstruction:
            scopes = ""
            for expr in inst.scopeKindExprs:
                scopes += ", " + expr
            line("{")
            with indent():
                line("auto result = popScope(scopePosition" + scopes + ");")
                line("if (result == nullptr) {")
                with indent():
                    generateError(case)
                line("}")
                line("scopePosition = result;")
            line("}")
        elif type(inst) is IfScopeInstruction:
            check = ""
            for scope in inst.scopeKindExprs:
                check += " || scopePosition[0] == " + scope
            check = check[4:]
            line("if (" + check + ") {")
            with indent():
                generateInstructions(case, inst.instructions, thenLabel)
            line("}")
        elif type(inst) is ThenInstruction:
            line("goto " + inst.newState + "$" + thenLabel + ";")
        elif type(inst) is ErrorInstruction:
            generateError(case)
        else:
            raise Exception("invalid instruction \"" + inst.format() + "\"")

def collectOrigins(state, instructions):
    for inst in instructions:
        if (type(inst) is NextInstruction or type(inst) is ThenInstruction) and inst.newState == state.name:
            state.origins.append(inst)
        if type(inst) is IfScopeInstruction:
            collectOrigins(state, inst.instructions)

for state in states:
    state.origins = []
    for s in states:
        if s.thenState == state.name:
            state.origins.append(s)
        for c in s.cases:
            collectOrigins(state, c.instructions)


def shouldBeInlined(state):
    return len(state.origins) == 1 and state.kind == "LinearState" and type(state.origins[0]) is NextInstruction

def onlyUsedAsThen(state):
    if [o for o in state.origins if type(o) is NextInstruction]:
        return False
    return True

line("switch (parseState) {")
for state in states:
    line("case State::" + stateCppName(state.name) + ":")
    with indent():
        if shouldBeInlined(state) or onlyUsedAsThen(state):
            line("VERIFY_NOT_REACHED();")
        else:
            line("goto " + state.name + "$no_emit;")
line("}")

for state in states:
    if len(state.origins) == 0:
        raise Exception("unused state '" + state.name + "'")
    if not shouldBeInlined(state):
        line("// " + state.kind + " " + state.name)
        withEmitLabelUsed = [o for o in state.origins if type(o) is NextInstruction and o.carriesEmitNode]
        if withEmitLabelUsed:
            labelLine(state.name + "$with_emit:")
            emitCarriedNode()
        noEmitLabelUsed = not onlyUsedAsThen(state)
        if noEmitLabelUsed:
            labelLine(state.name + "$no_emit:")
        if noEmitLabelUsed or withEmitLabelUsed:
            if state.kind == "SwitchState":
                line("tokEnd = skipWhitespace(tokEnd);")
                line("tokBegin = tokEnd;")
            else:
                inlineTokenAdvancer()
            rememberState(state)
        if [o for o in state.origins if not type(o) is NextInstruction]:
            labelLine(state.name + "$as_then:")

        generateState(state)

        lineNoIndent()

# combine/write
currentDir = pathlib.Path(__file__).parent.resolve()
with open(currentDir / "parse.cpp.in", "r") as f:
    inputLines = f.readlines()

outputLines = []
lineEnding = None
for inputLine in inputLines:
    strippedLine = inputLine.lstrip(' ')
    if strippedLine.startswith("// GENERATED CODE HERE"):
        extraIndent = len(inputLine) - len(strippedLine) - 4
        lineEnding = inputLine[len(inputLine.rstrip('\r\n')):]
        for generatedLine in generatedLines:
            if len(generatedLine) > 0:
                outputLines.append(' ' * extraIndent + generatedLine + lineEnding)
            else:
                outputLines.append(lineEnding)
    else:
        outputLines.append(inputLine)

with open(currentDir / "parse.cpp", "w") as f:
    f.writelines(outputLines)

# generate .h
generatedLines = []
outputIndentation = 0
lineNoIndent("#pragma once")
lineNoIndent()
lineNoIndent("#include \"WordTable.h\"")
lineNoIndent()
line("namespace parse {")
lineNoIndent()

line("inline constexpr ConstWordStringTable words {")
with indent():
    for keyword in keywords:
        line("keyword(\"" + keyword + "\"),")
line("};")

line("enum class Token : uint8_t {")
with indent():
    for punc in punctuationTokens:
        line(punctuationCppName(punc) + ", // " + punc)
    for keyword in keywords:
        line(keywordCppName(keyword) + ", // " + keyword)
    line(identifierCppName() + ",")
line("};")
line("std::string_view nameString(Token);")
lineNoIndent()

line("enum class State {")
with indent():
    for state in states:
        line(stateCppName(state.name) + ",")
line("};")
line("std::string_view nameString(State);")
lineNoIndent()
line("}")

outputLines = []
for generatedLine in generatedLines:
    outputLines.append(generatedLine + lineEnding)
with open(currentDir / "parse_gen.h", "w") as f:
    f.writelines(outputLines)

# generate .cpp
generatedLines = []
outputIndentation = 0
lineNoIndent("#include \"parse.h\"")
lineNoIndent()
line("namespace parse {")
lineNoIndent()

line("std::string_view nameString(Token token) {")
with indent():
    line("switch (token) {")
    for punc in punctuationTokens:
        line("case Token::" + punctuationCppName(punc) + ":")
        with indent():
            line("return \"" + punctuationCppName(punc) + "\";")
    for keyword in keywords:
        line("case Token::" + keywordCppName(keyword) + ":")
        with indent():
            line("return \"" + keywordCppName(keyword) + "\";")
    line("case Token::" + identifierCppName() + ":")
    with indent():
        line("return \"" + identifierCppName() + "\";")
    line("}")
line("}")
lineNoIndent()

line("std::string_view nameString(State state) {")
with indent():
    line("switch (state) {")
    for state in states:
        line("case State::" + stateCppName(state.name) + ":")
        with indent():
            line("return \"" + stateCppName(state.name) + "\";")
    line("}")
line("}")
lineNoIndent()
line("}")

outputLines = []
for generatedLine in generatedLines:
    outputLines.append(generatedLine + lineEnding)
with open(currentDir / "parse_gen.cpp", "w") as f:
    f.writelines(outputLines)
