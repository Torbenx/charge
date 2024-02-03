import string
import pathlib
import dataclasses

punctuations = [
    "(", ")", "[", "]", "{", "}",
    "?", "!", "~", "++", "--", "+", "-", "*",
    "&", "^", "|", "/", "%", "<<", ">>", "&&", "||", "!=", "==", "<", "<=", ">", ">=",
    "=", "+=", "-=", "*=", "&=", "^=", "|=", "/=", "%=", "<<=", ">>=", "&&=", "||=",
    ",", ".", ":", "::", ";", "=>", "<=>", "->",
    "//", "/*"
]
keywords = [
    "if", "elif", "else", "match", "for", "while", "do", "return", "break", "continue", "loop", "guard", "try", "catch", "with", "analysis", "assert",
    "namespace", "struct", "trait", "object", "fn", "static",
    "template",
    "var", "let", "in", "inout", "out", "forward", "assign",
    "true", "false",
]

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

class KeywordCase(Case):
    def __init__(self, keyword, instructions):
        super().__init__(instructions)
        self.keyword = keyword

class IdentifierCase(Case):
    def __init__(self, instructions):
        super().__init__(instructions)

class ThenCase(Case):
    def __init__(self, instructions):
        super().__init__(instructions)

@dataclasses.dataclass
class NextInstruction:
    newState: str
    carriesEmitNode: bool = False
    def format(self):
        return "next " + self.newState

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
class ExitIfUnscopedInstruction:
    def format(self):
        return "exitIfUnscoped"

@dataclasses.dataclass
class AssignInstruction:
    leftName: str
    rightExpr: str
    def format(self):
        return self.leftName + " = " + self.rightExpr

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

    def keywordCase(self, keyword: str) -> KeywordCase | None:
        for c in self.cases:
            if type(c) is KeywordCase and c.keyword == keyword:
                return c
        return None

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

class Parser:
    def __init__(self, lines):
        self.lines = lines
        self.line = ""
        self.lineNumber = 0
        self.advanceLine()

    def lineEmpty(self):
        return len(self.line.lstrip("\r\n ")) == 0

    def lineLevel(self):
        if self.line.startswith(indentationStep * 2):
            return 2
        if self.line.startswith(indentationStep):
            return 1
        return 0

    def atEnd(self):
        return len(self.lines) == 0

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
        return self.parseWord("()[]{}?!~+-*&^|/%<>=,.;:")

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
            else:
                self.parseError("invalid case '" + caseKind + "'")
        return cases

    def parseInstructions(self):
        instructions = []
        while self.lineLevel() == 2:
            first = self.parseWord()
            if first == "next":
                delayed = False
                if len(instructions) > 0 and type(instructions[-1]) == EmitNodeInstruction and instructions[-1].tokenExpr is None:
                    instructions[-1].delayed = True
                    delayed = True
                instructions.append(NextInstruction(self.parseWord(), delayed))
            elif first == "emitNode":
                nodeKindExpr = self.parseExpr()
                while self.line[0] == ' ':
                    self.line = self.line[1:]
                tokenExpr = None
                if self.line[0] == ',':
                    self.line = self.line[1:]
                    tokenExpr = self.parseExpr()
                instructions.append(EmitNodeInstruction(nodeKindExpr, tokenExpr))
            elif first == "pushScope":
                instructions.append(PushScopeInstruction(self.parseExpr()))
            elif first == "popScope":
                scopes = []
                while not self.lineEmpty():
                    scopes.append(self.parseExpr())
                    if self.line[0] == ',':
                        self.line = self.line[1:]
                instructions.append(PopScopeInstruction(scopes))
            elif first == "exitIfUnscoped":
                instructions.append(ExitIfUnscopedInstruction())
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
    puncs = list(filter(lambda p: (p.startswith(commonPrefix)), punctuations))
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
        line("tokEnd = skipToEndOfLine(tokEnd);")
        line("emitNode(NodeKind::LineComment, tokBegin, tokEnd, state, sourceBufferBegin);")
        line("goto " + state.name + "_no_emit;")
    elif exactMatch == "/*":
        line("tokEnd = skipToEndOfBlockComment(tokEnd);")
        line("tokEnd += 2;")
        line("emitNode(NodeKind::BlockComment, tokBegin, tokEnd, state, sourceBufferBegin);")
        line("goto " + state.name + "_no_emit;")
    else:
        foundState, case = recurse(state, lambda s: s.punctuationCase(exactMatch))
        generateCaseBody(foundState, case)

def checkForPunctuation(punc, handler):
    puncs = list(filter(lambda p: (p.startswith(punc)), punctuations))
    assert(len(puncs) > 0)
    line("if (std::string_view(tokEnd, " + str(len(punc)) + ") == \"" + punc + "\") {")
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

def checkForKeyword(keyword, handler):
    line("if (std::string_view(tokEnd, " + str(len(keyword)) + ") == \"" + keyword + "\" && !isWordBulkCharacter(tokEnd[" + str(len(keyword)) + "])) {")
    with indent():
        line("tokEnd += " + str(len(keyword)) + ";")
        handler()
    line("}")

def inlineTokenAdvancer():
    line("tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);")
    line("tokBegin = tokEnd;")

def emitNode(nodeKindExpr, beginExpr, endExpr):
    line("emitNode(" + nodeKindExpr + ", " + beginExpr +", " + endExpr + ", state, sourceBufferBegin);")

def emitCarriedNode():
    emitNode("carriedEmitNodeKind", "tokBegin", "tokEnd")

errorCases  = [PunctuationCase(p, [ErrorInstruction()]) for p in punctuations]
errorCases += [KeywordCase(k, [ErrorInstruction()]) for k in keywords]
errorCases += [IdentifierCase([ErrorInstruction()])]
errorState = State("SwitchState", "error", "", [], errorCases)

def findState(name: str) -> State:
    if name == "error":
        return errorState
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
    line("case '\\n': {")
    with indent():
        line("tokEnd += 1;")
        line("goto " + state.name + "_no_emit;")
    line("}")

    line("case '\\r': {")
    with indent():
        line("if (tokEnd[1] == '\\n') {")
        with indent():
            line("tokEnd += 2;")
            line("goto " + state.name + "_no_emit;")
        line("}")
        line("tokEnd += 1;")
        line("goto " + state.name + "_no_emit;")
    line("}")

    # punctuations
    firstCharacters = {p[0] for p in punctuations}
    for character in sorted(firstCharacters):
        line("case '" + character + "': {")
        with indent():
            linearIf(str(character), state)
        line("}")

    # word
    identifierBeginCharacters = set(string.ascii_lowercase) | set(string.ascii_uppercase)
    keywordBeginCharacters = {k[0] for k in keywords}

    for character in sorted(keywordBeginCharacters):
        line("case '" + character + "': {")
        with indent():
            for keyword in keywords:
                if keyword.startswith(character):
                    line("if (std::string_view(tokEnd + 1, " + str(len(keyword) - 1) + ") == \"" + keyword[1:] + "\" && !isWordBulkCharacter(tokEnd[" + str(len(keyword)) + "])) {")
                    with indent():
                        line("tokEnd += " + str(len(keyword))+ ";")
                        foundState, case = recurse(state, lambda s: s.keywordCase(keyword))
                        generateCaseBody(foundState, case)
                    line("}")
            line("goto " + state.name + "_identifier_case;")
        line("}")

    for character in sorted(identifierBeginCharacters - keywordBeginCharacters):
        line("case '" + character + "':")
    line("case '#':")
    line("case '$':")
    line("case '_': {")
    with indent():
        line("goto " + state.name + "_identifier_case;")
    line("}")

    # default
    line("default: {")
    with indent():
        line("return state.nodes;")
        #line("TODO_ERROR(\"invalid character\");")
    line("}")

    line("} // switch")

    line("VERIFY_NOT_REACHED();")

    labelLine(state.name + "_identifier_case:")
    line("tokEnd = skipToEndOfIdentifier(tokEnd);")
    foundState, case = recurse(state, lambda s: s.identifierCase())
    generateCaseBody(foundState, case)

def generateLinearState(state):
    for c in state.cases:
        if type(c) == PunctuationCase:
            checkForPunctuation(c.punctuation, lambda: generateCaseBody(state, c))
        elif type(c) == KeywordCase:
            checkForKeyword(c.keyword, lambda: generateCaseBody(state, c))
        elif type(c) == IdentifierCase:
            line("if (isWordFirstCharacter(tokEnd[0])) {")
            with indent():
                line("tokEnd = skipToEndOfIdentifier(tokEnd);")
                generateCaseBody(state, c)
            line("}")
    thenCase = state.thenCase()
    if not thenCase is None:
        generateCaseBody(state, thenCase)
    line("// then " + state.thenState)
    line("goto " + state.thenState + "_no_whitespace;")

def generateCaseBody(state, case):
    for inst in case.instructions:
        line("// " + inst.format())
        if type(inst) is EmitNodeInstruction:
            if inst.delayed:
                assert(inst.tokenExpr is None)
                line("carriedEmitNodeKind = "+ inst.nodeKindExpr + ";")
            elif inst.tokenExpr is None:
                emitNode(inst.nodeKindExpr, "tokBegin", "tokEnd")
            else:
                emitNode(inst.nodeKindExpr, inst.tokenExpr + "Begin", inst.tokenExpr + "End")
        elif type(inst) is NextInstruction:
            newState = findState(inst.newState)
            if shouldBeInlined(newState):
                line("// inlined " + newState.name)
                assert(newState.origins == [inst])
                assert(newState.kind == "LinearState")
                if inst.carriesEmitNode:
                    emitCarriedNode()
                inlineTokenAdvancer()
                generateState(newState)
            else:
                line("goto " + newState.name + ("_with_emit" if inst.carriesEmitNode else "_no_emit") + ";")
        elif type(inst) is AssignInstruction:
            if inst.leftName == "savedToken":
                line("savedTokenBegin = " + inst.rightExpr + "Begin;")
                line("savedTokenEnd = " + inst.rightExpr + "End;")
            elif inst.leftName == "nodeKind":
                line("nodeKind = " + inst.rightExpr + ";")
            else:
                assert(False)
        elif type(inst) is PushScopeInstruction:
            line("scopePosition = pushScope(scopePosition, " + inst.scopeKindExpr + ");")
        elif type(inst) is PopScopeInstruction:
            scopes = ""
            for expr in inst.scopeKindExprs:
                scopes += ", " + expr
            line("scopePosition = popScope(scopePosition" + scopes + ");")
        elif type(inst) is ExitIfUnscopedInstruction:
            line("if (ScopeBuffer::toIndex(scopePosition) == 0) {")
            with indent():
                line("return state.nodes;")
            line("}")
        elif type(inst) is ErrorInstruction:
            line("VERIFY_NOT_REACHED();")
        else:
            raise Exception("invalid instruction \"" + inst.format() + "\"")

for state in states:
    state.origins = []
    for s in states:
        if s.thenState == state.name:
            state.origins.append(s)
        for c in s.cases:
            inst = c.instructions[-1]
            if type(inst) == NextInstruction and inst.newState == state.name:
                state.origins.append(inst)

def shouldBeInlined(state):
    return len(state.origins) == 1 and state.kind == "LinearState"

for state in states:
    if len(state.origins) == 0:
        raise Exception("unused state '" + state.name + "'")
    if not shouldBeInlined(state):
        line("// " + state.kind + " " + state.name)
        label1Used = [o for o in state.origins if type(o) is NextInstruction and o.carriesEmitNode]
        if label1Used:
            labelLine(state.name + "_with_emit:")
            emitCarriedNode()
        label2Used = state.kind == "SwitchState" or [o for o in state.origins if type(o) is NextInstruction and not o.carriesEmitNode]
        if label2Used:
            labelLine(state.name + "_no_emit:")
        if label1Used or label2Used:
            if state.kind == "SwitchState":
                line("tokEnd = skipWhitespace(tokEnd);")
                line("tokBegin = tokEnd;")
            else:
                inlineTokenAdvancer()
        if [o for o in state.origins if type(o) is State]:
            labelLine(state.name + "_no_whitespace:")

        generateState(state)

        lineNoIndent()

# combine/write
currentDir = pathlib.Path(__file__).parent.resolve()
with open(currentDir / "parse.cpp.in", "r") as f:
    inputLines = f.readlines()

outputLines = []
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
