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

# Words that are their own token
keywords = [
    "assert",
    "break",
    "catch",
    "const",
    "continue",
    "destroy",
    "discard",
    "do",
    "elif",
    "else",
    "for",
    "if",
    "impl",
    "in",
    "let",
    "loop",
    "return",
    "shared",
    "static",
    "try",
    "unique",
    "var",
    "while",
]
# Identifiers that have spacial meaning in some context
specialIdentifiers = [
    "base",
    "context",
    "enum",
    "fn",
    "incomplete",
    "namespace",
    "open",
    "struct",
    "template",
    "trait",
    "virtual",
]
# Identifiers that have no special meaning in any context
# but still need to be available at compile time.
regularIdentifiers = [
    "bool",
    "const_shared_ref",
    "const_unique_ref",
    "copy",
    "error",
    "expression_category",
    "false",
    "from",
    "function_id",
    "function_signature",
    "logical_not",
    "member_ptr",
    "member_type",
    "return_type",
    "parent_type",
    "pointee_type",
    "ptr",
    "self_type",
    "self",
    "shared_ref",
    "sig",
    "T",
    "template_id",
    "template_signature",
    "true",
    "type",
    "unique_ref",
    "value",
    "(generated_identifier)",
]

punctuations = punctuationTokens + ["//", "/*"]
punctuationAlphabet = "".join(sorted({p[0] for p in punctuations}))

def punctuationCppName(punc):
    characterNames = {
        '(': "LeftParen", ')': "RightParen", '[': "LeftSquare", ']': "RightSquare", '{': "LeftBrace", '}': "RightBrace",
        '!': "Exclaim", '~': "Tilde", '+': "Plus", '-': "Minus", '*': "Star",
        '&': "Amp", '^': "Hat", '|': "Vert", '/': "Slash", '%': "Percent", '<': "Less", '>': "Greater",
        '=': "Equal", ',': "Comma", '.': "Point", ';': "SemiColon", ':': "Colon"
    }
    return "".join([characterNames[c] for c in punc])

def keywordCppName(keyword):
    return keyword[0].upper() + keyword[1:]

def specialIdentifierCppName(identifier):
    return identifier[0].upper() + identifier[1:]

def stateCppName(name):
    parts = name.split('_')
    parts = [p[0].upper() + p[1:] for p in parts]
    return "".join(parts)

def identifierCppName():
    return "Identifier"

def characterLiteralCppName():
    return "CharacterLiteral"

def numericLiteralCppName():
    return "NumericLiteral"

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

class SpecialIdentifierCase(Case):
    def __init__(self, identifier, instructions):
        super().__init__(instructions)
        self.identifier = identifier
    def cppName(self):
        return specialIdentifierCppName(self.identifier)

class IdentifierCase(Case):
    def __init__(self, instructions):
        super().__init__(instructions)
    def cppName(self):
        return identifierCppName()

class CharacterLiteralCase(Case):
    def __init__(self, instructions):
        super().__init__(instructions)
    def cppName(self):
        return characterLiteralCppName()

class NumericLiteralCase(Case):
    def __init__(self, instructions):
        super().__init__(instructions)
    def cppName(self):
        return numericLiteralCppName()

class ThenCase(Case):
    def __init__(self, instructions):
        super().__init__(instructions)

class DispatchCase(Case):
    def __init__(self, instructions):
        super().__init__(instructions)

class EndCase(Case):
    def __init__(self, instructions):
        super().__init__(instructions)
    def cppName(self):
        return "EOS"

@dataclasses.dataclass
class NextInstruction:
    newState: str
    carriesEmitToken: bool = False
    def format(self):
        return "next " + self.newState

@dataclasses.dataclass
class ThenInstruction:
    newState: str
    def format(self):
        return "then " + self.newState

@dataclasses.dataclass
class EmitTokenInstruction:
    tokenKindExpr: str
    dataExpr: str | None = None
    delayed: bool = False
    def format(self):
        ret = "emitToken " + self.tokenKindExpr
        if not self.dataExpr is None:
            ret += ", " + self.dataExpr
        return ret

@dataclasses.dataclass
class UpdateKindInstruction:
    tokenKindExpr: str
    def format(self):
        return "updateKind " + self.tokenKindExpr

@dataclasses.dataclass
class DiscardLastTokenInstruction:
    def format(self):
        return "discardLastToken"

@dataclasses.dataclass
class UpdateDataInstruction:
    tokenDataExpr: str
    def format(self):
        return "updateData " + self.tokenDataExpr

@dataclasses.dataclass
class UpdateSecondaryDataInstruction:
    tokenDataExpr: str
    def format(self):
        return "updateSecondaryData " + self.tokenDataExpr

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
class CommitDeclarationInstruction:
    declKindExpr: str
    nameExpr: str | None

    def format(self):
        ret = "commitDeclaration " + self.declKindExpr
        if not self.nameExpr is None:
            ret += ", " + self.nameExpr
        return ret

@dataclasses.dataclass
class CommitImplDeclarationInstruction:
    declKindExpr: str

    def format(self):
        return "commitImplDeclaration " + self.declKindExpr

@dataclasses.dataclass
class RememberDeclarationBeginInstruction:
    def format(self):
        return "rememberDeclarationBegin"

@dataclasses.dataclass
class EndDeclarationInstruction:
    def format(self):
        return "endDeclaration"

@dataclasses.dataclass
class CallArgumentInstruction:
    nameExpr: str | None
    def format(self):
        ret = "callArgument"
        if not self.nameExpr is None:
            ret += " " + self.nameExpr
        return ret

@dataclasses.dataclass
class UpdateCallArgumentInstruction:
    nameExpr: str | None
    def format(self):
        ret = "callArgument"
        if not self.nameExpr is None:
            ret += " " + self.nameExpr
        return ret

@dataclasses.dataclass
class EmitCallTokenInstruction:
    tokenKindExpr: str
    def format(self):
        return "emitCallToken " + self.tokenKindExpr

@dataclasses.dataclass
class EndCallInstruction:
    def format(self):
        return "endCall"

@dataclasses.dataclass
class SetGlobalKindInstruction:
    globalKindExpr: str
    def format(self):
        return "setGlobalKind " + self.globalKindExpr

@dataclasses.dataclass
class LexTokenInstruction:
    def format(self):
        return "lexToken"

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

    def specialIdentifierCase(self, identifier: str) -> SpecialIdentifierCase | None:
        for c in self.cases:
            if type(c) is SpecialIdentifierCase and c.identifier == identifier:
                return c
        return None

    def specialIdentifierCases(self) -> [SpecialIdentifierCase]:
        return [c for c in self.cases if type(c) is SpecialIdentifierCase]

    def identifierCase(self) -> IdentifierCase | None:
        for c in self.cases:
            if type(c) is IdentifierCase:
                return c
        return None

    def hasWordCase(self):
        return self.identifierCase() or self.keywordCases() or self.specialIdentifierCases()

    def characterLiteralCase(self) -> CharacterLiteralCase | None:
        for c in self.cases:
            if type(c) is CharacterLiteralCase:
                return c
        return None

    def numericLiteralCase(self) -> NumericLiteralCase | None:
        for c in self.cases:
            if type(c) is NumericLiteralCase:
                return c
        return None

    def thenCase(self) -> ThenCase | None:
        for c in self.cases:
            if type(c) is ThenCase:
                return c
        return None

    def dispatchCase(self) -> DispatchCase | None:
        for c in self.cases:
            if type(c) is DispatchCase:
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
            if stateKind not in ["SwitchState", "LinearState"]:
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
                if keyword in keywords:
                    cases.append(KeywordCase(keyword, instructions))
                elif keyword in specialIdentifiers:
                    cases.append(SpecialIdentifierCase(keyword, instructions))
                else:
                    raise Exception(f"'{keyword}' is neither a keyword nor a special identifier")
            elif caseKind == "identifier":
                self.advanceLine()
                instructions = self.parseInstructions()
                cases.append(IdentifierCase(instructions))
            elif caseKind == "characterLiteral":
                self.advanceLine()
                instructions = self.parseInstructions()
                cases.append(CharacterLiteralCase(instructions))
            elif caseKind == "numericLiteral":
                self.advanceLine()
                instructions = self.parseInstructions()
                cases.append(NumericLiteralCase(instructions))
            elif caseKind == "then":
                self.advanceLine()
                instructions = self.parseInstructions()
                cases.append(ThenCase(instructions))
            elif caseKind == "dispatch":
                self.advanceLine()
                instructions = self.parseInstructions()
                cases.append(DispatchCase(instructions))
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
                if len(instructions) > 0 and type(instructions[-1]) == EmitTokenInstruction:
                    instructions[-1].delayed = True
                    delayed = True
                instructions.append(NextInstruction(self.parseWord(), delayed))
                self.advanceLine()
            elif first == "emitToken":
                tokenKindExpr = self.parseExpr()
                while self.line[0] == ' ':
                    self.line = self.line[1:]
                dataExpr = None
                if self.line[0] == ',':
                    self.line = self.line[1:]
                    dataExpr = self.parseExpr()
                instructions.append(EmitTokenInstruction(tokenKindExpr, dataExpr))
                self.advanceLine()
            elif first == "updateKind":
                instructions.append(UpdateKindInstruction(self.parseExpr()))
                self.advanceLine()
            elif first == "discardLastToken":
                instructions.append(DiscardLastTokenInstruction())
                self.advanceLine()
            elif first == "updateData":
                instructions.append(UpdateDataInstruction(self.parseExpr()))
                self.advanceLine()
            elif first == "updateSecondaryData":
                instructions.append(UpdateSecondaryDataInstruction(self.parseExpr()))
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
            elif first == "commitDeclaration":
                declKindExpr = self.parseExpr()
                while self.line[0] == ' ':
                    self.line = self.line[1:]
                nameExpr = None
                if self.line[0] == ',':
                    self.line = self.line[1:]
                    nameExpr = self.parseExpr()
                instructions.append(CommitDeclarationInstruction(declKindExpr, nameExpr))
                self.advanceLine()
            elif first == "commitImplDeclaration":
                instructions.append(CommitImplDeclarationInstruction(self.parseExpr()))
                self.advanceLine()
            elif first == "rememberDeclarationBegin":
                instructions.append(RememberDeclarationBeginInstruction())
                self.advanceLine()
            elif first == "endDeclaration":
                instructions.append(EndDeclarationInstruction())
                self.advanceLine()
            elif first == "callArgument":
                nameExpr = None
                if not self.lineEmpty():
                    nameExpr = self.parseExpr()
                instructions.append(CallArgumentInstruction(nameExpr))
                self.advanceLine()
            elif first == "updateCallArgument":
                nameExpr = None
                if not self.lineEmpty():
                    nameExpr = self.parseExpr()
                instructions.append(UpdateCallArgumentInstruction(nameExpr))
                self.advanceLine()
            elif first == "emitCallToken":
                instructions.append(EmitCallTokenInstruction(self.parseExpr()))
                self.advanceLine()
            elif first == "endCall":
                instructions.append(EndCallInstruction())
                self.advanceLine()
            elif first == "setGlobalKind":
                instructions.append(SetGlobalKindInstruction(self.parseExpr()))
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
generateSingleStep = False

generateStateDebug = False
generateLexTokenChecks = False

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
            assert exactMatch == None
            exactMatch = p
        else:
            possibleContinuations.add(p[len(commonPrefix)])
    assert exactMatch != None

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
            line("emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, output);")
            line("goto " + state.name + "$retry;")
    elif exactMatch == "/*":
        if onlyUsedAsThen(state):
            line("VERIFY_NOT_REACHED();")
        else:
            line("tokEnd = skipToEndOfBlockComment(tokEnd);")
            line("tokEnd += 2;")
            line("emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, output);")
            line("goto " + state.name + "$retry;")
    else:
        recurse(state, lambda s: s.punctuationCase(exactMatch), lambda case: generateCaseBody(case))

def checkForPunctuation(punc, handler):
    puncs = sorted(filter(lambda p: (p.startswith(punc)), punctuations))
    assert len(puncs) > 0
    line("if (std::string_view(tokEnd, " + str(len(punc)) + ") == \"" + punc + "\"sv) {")
    with indent():
        if len(puncs) == 1:
            assert puncs[0] == punc
            line("tokEnd += " + str(len(punc)) + ";")
            handler()
        else:
            possibleContinuations = set()
            for p in puncs:
                if p != punc:
                    possibleContinuations.add(p[len(punc)])
            line("char next = tokEnd[" + str(len(punc)) + "];")
            condition = ""
            for c in sorted(possibleContinuations):
                condition += " && next != '" + c + "'"
            condition = condition[4:]
            line("if (" + condition + ") {")
            with indent():
                line("tokEnd += " + str(len(punc)) + ";")
                handler()
            line("}")
    line("}")

def inlineTokenAdvancer():
    line("tokEnd = inlineAdvancer(tokEnd, output);")
    line("tokBegin = tokEnd;")

def emitToken(tokenKindExpr, tokenData = "0"):
    line("emitToken(" + tokenKindExpr + ", tokBegin, " + tokenData + ", output);")

def emitCarriedToken():
    emitToken("carriedEmitTokenKind", "carriedEmitTokenData")

def rememberState(state):
    line("parseState = State::" + stateCppName(state.name) + ";")

def collectPossibleThenStatesFromInstructions(instructions, result):
    for inst in instructions:
        if type(inst) is IfScopeInstruction:
            collectPossibleThenStatesFromInstructions(inst.instructions, result)
        if type(inst) is ThenInstruction or type(inst) is NextInstruction:
            if not inst.newState in result:
                result.add(inst.newState)
                collectPossibleThenStatesInto(findState(inst.newState), result)

def collectPossibleThenStatesInto(state, result):
    if state.name == "error":
        return
    if state.thenCase():
        collectPossibleThenStatesFromInstructions(state.thenCase().instructions, result)
    if state.dispatchCase():
        collectPossibleThenStatesFromInstructions(state.dispatchCase().instructions, result)
    if not state.thenState in result:
        result.add(state.thenState)
        collectPossibleThenStatesInto(findState(state.thenState), result)

def collectPossibleThenStates(state):
    result = set()
    collectPossibleThenStatesInto(state, result)
    return result

def collectPossibleCases(originalState):
    statesToConsider = collectPossibleThenStates(originalState)
    statesToConsider.add(originalState.name)
    result = set()
    for stateName in statesToConsider:
        state = findState(stateName)
        for case in state.cases:
            if type(case) is ThenCase or type(case) is DispatchCase or type(case) is EndCase:
                continue
            result.add(case.cppName())
    return result

def readWord():
    line("{")
    with indent():
        line("auto wordAndPos = readWord(tokEnd, output);")
        line("tokEnd = wordAndPos.position;")
        line("this_identifier = wordAndPos.word;")
    line("}")

def generateWordCase(state):
    def caseLabel(keyword):
        labelLine("case toCaseValue<identifier_t>(LexerToken::" + keywordCppName(keyword) + ", \"" + keyword + "\"):")

    labelLine("LABEL_MAYBE_UNUSED " + state.name + "$word_case_with_read:")
    readWord()
    labelLine("LABEL_MAYBE_UNUSED " + state.name + "$word_case:")
    keywords = set()
    specialIds = set()
    for c in state.keywordCases():
        keywords.add(c.keyword)
    for c in state.specialIdentifierCases():
        specialIds.add(c.identifier)
    performExaustiveMatch = state.identifierCase() is not None or True
    if performExaustiveMatch:
        for thenName in collectPossibleThenStates(state):
            then = findState(thenName)
            for c in then.keywordCases():
                keywords.add(c.keyword)
            for c in then.specialIdentifierCases():
                specialIds.add(c.identifier)
    line("if (isIdentifierKeywordOrSpecial(this_identifier)) {")
    with indent():
        line("switch (toSwitchValue(this_identifier)) {")
        with indent():
            for keyword in sorted(keywords):
                caseLabel(keyword)
                recurse(state, lambda s: s.keywordCase(keyword), lambda c: generateCaseBody(c))
            for specialId in sorted(specialIds):
                caseLabel(specialId)
                recurse(state, lambda s: s.specialIdentifierCase(specialId), lambda c: generateCaseBody(c))
            labelLine("default:")
            if performExaustiveMatch:
                line("if (isKeyword(this_identifier)) {")
                with indent():
                    line("goto error$as_then;")
                line("}")
            line("break;")
        line("}")
    line("}")
    if performExaustiveMatch:
        recurse(state, lambda s: s.identifierCase(), lambda c: generateCaseBody(c))
    else:
        recurse(state, lambda s: s if s != state and s.hasWordCase() else None, lambda s: line("goto " + s.name + "$word_case;"))

def generateEndCase(case):
    endedWithJumpInstruction = generateCaseBody(case)
    if not endedWithJumpInstruction:
        emitToken("TokenKind::EOS")
        line("emitWhitespace(WhitespaceKind::EOS, tokBegin, tokEnd, output);")
        line("goto exit;")

errorThenCase = ThenCase([])
errorState = State("SwitchState", "error", "", [], [errorThenCase])
nonErrorStates = states.copy()
states += [errorState]

def findState(name: str) -> State:
    global states
    for s in states:
        if s.name == name:
            return s
    raise Exception("state '" + name + "' undefined")

def recurse(state, check, func):
    while True:
        val = check(state)
        if val:
            func(val)
            return
        thenCase = state.thenCase()
        thenCaseClosed = False
        if not thenCase is None:
            thenCaseClosed = generateCaseBody(thenCase, lambda target: recurse(findState(target), check, func))
        if thenCaseClosed:
            return
        line("// -> " + state.thenState)
        if generateStateDebug:
            line("dbgln(\" -> " + state.thenState + "\");")
        state = findState(state.thenState)

def generateState(state):
    if state.kind == "SwitchState":
        generateSwitchState(state)
    elif state.kind == "LinearState":
        generateLinearState(state)
    else:
        assert False

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
            line("markLineBegin(tokEnd, output);")
            line("goto " + state.name + "$retry;")
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
            line("markLineBegin(tokEnd, output);")
            line("goto " + state.name + "$retry;")
        line("}")

    # punctuations
    firstCharacters = {p[0] for p in punctuations}
    for character in sorted(firstCharacters):
        line("case '" + character + "': {")
        with indent():
            linearIf(str(character), state)
        line("}")

    # integer literal
    line("case '0':")
    line("case '1':")
    line("case '2':")
    line("case '3':")
    line("case '4':")
    line("case '5':")
    line("case '6':")
    line("case '7':")
    line("case '8':")
    line("case '9': {")
    with indent():
        line("do {")
        with indent():
            line("tokEnd += 1;")
        line("} while (tokEnd[0] >= '0' && tokEnd[0] <= '9');")
        recurse(state, lambda s: s.numericLiteralCase(), lambda case: generateCaseBody(case))
    line("}")

    # character literal
    line("case '\\'': {")
    with indent():
        line("tokEnd = skipToEndOfCharacterLiteral(tokEnd);")
        line("VERIFY(tokEnd[0] == '\\'');")
        line("tokEnd += 1;")
        recurse(state, lambda s: s.characterLiteralCase(), lambda case: generateCaseBody(case))
    line("}")

    # end
    line ("case '\\0':")
    with indent():
        recurse(state, lambda s: s.endCase(), lambda case: generateEndCase(case))

    # word
    for character in string.ascii_lowercase + string.ascii_uppercase:
        line("case '" + character + "':")
    line("case '#':")
    line("case '$':")
    line("case '_':")
    with indent():
        recurse(state, lambda s: s if s.hasWordCase() else None, lambda s: line("goto " + s.name + "$word_case_with_read;"))

    # default
    line("default: {")
    with indent():
        line("VERIFY_NOT_REACHED();")
    line("}")

    line("} // switch")

    line("VERIFY_NOT_REACHED();")

    if state.hasWordCase():
        generateWordCase(state)

def generateLinearState(state):
    for c in state.punctuationCases():
        checkForPunctuation(c.punctuation, lambda: generateCaseBody(c))

    if state.hasWordCase():
        line("if (isWordFirstCharacter(tokEnd[0])) {")
        with indent():
            generateWordCase(state)
        line("}")

    endCase = state.endCase()
    if not endCase is None:
        line("if (tokEnd[0] == '\\0') {")
        with indent():
            generateEndCase(endCase)
        line("}")

    thenCase = state.thenCase()
    def generateThenJump(target):
        if generateStateDebug:
            line("dbgln(\" -> " + target + "\");")
        line("goto " + target + "$as_then;")
    thenCaseClosed = False
    if not thenCase is None:
        thenCaseClosed = generateCaseBody(thenCase, lambda target: generateThenJump(target))
    if not thenCaseClosed:
        line("// then " + state.thenState)
        generateThenJump(state.thenState)

def generateCaseBody(case, thenHandler = lambda target: line("#error Use of stub thenHandler")):
    if case is errorThenCase:
        line("goto error$as_then;")
        return True
    return generateInstructions(case, case.instructions, thenHandler)

def generateInstructions(case, instructions, thenHandler):
    afterJumpInstruction = False
    for inst in instructions:
        if afterJumpInstruction:
            raise Exception("unreachable code")
        line("// " + inst.format())
        if type(inst) is EmitTokenInstruction:
            if generateLexTokenChecks:
                lexToken = "Invalid" if type(case) is ThenCase else case.cppName()
                line("checkLexToken(" + inst.tokenKindExpr + ", LexerToken::" + lexToken + ");")
            dataExpr = "0"
            if type(case) is IdentifierCase:
                assert inst.dataExpr is None
                dataExpr = "packData1(" + inst.tokenKindExpr + ", this_identifier)"
            elif not inst.dataExpr is None:
                dataExpr = "packData1(" + inst.tokenKindExpr + ", " + inst.dataExpr + ")"
            if inst.delayed and not generateSingleStep:
                line("carriedEmitTokenKind = " + inst.tokenKindExpr + ";")
                line("carriedEmitTokenData = " + dataExpr + ";")
            else:
                emitToken(inst.tokenKindExpr, dataExpr)
        elif type(inst) is UpdateKindInstruction:
            line("setBackKind(output, " + inst.tokenKindExpr + ");")
        elif type(inst) is DiscardLastTokenInstruction:
            line("discardLastToken(output);")
        elif type(inst) is UpdateDataInstruction:
            line("setBackData1(output, " + inst.tokenDataExpr + ");")
        elif type(inst) is UpdateSecondaryDataInstruction:
            line("setBackData2(output, " + inst.tokenDataExpr + ");")
        elif type(inst) is NextInstruction:
            newState = findState(inst.newState)
            if generateSingleStep:
                line("goto single_step_complete;")
            else:
                line("goto " + newState.name + ("$with_emit" if inst.carriesEmitToken else "$no_emit") + ";")
            afterJumpInstruction = True
        elif type(inst) is AssignInstruction:
            line(inst.leftName + " = " + inst.rightExpr + ";")
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
                    line("goto pop_scope_failed;")
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
                generateInstructions(case, inst.instructions, thenHandler)
            line("}")
        elif type(inst) is ThenInstruction:
            thenHandler(inst.newState)
            afterJumpInstruction = True
        elif type(inst) is CommitDeclarationInstruction:
            nameExpr = "identifier_t()"
            if not inst.nameExpr is None:
                nameExpr = inst.nameExpr
            line("this_declaration = commitDeclaration<" + inst.declKindExpr + ">(" + nameExpr + ", tokBegin, declarationBegin, output);")
        elif type(inst) is CommitImplDeclarationInstruction:
            line("this_declaration = commitImplDeclaration<" + inst.declKindExpr + ">(tokBegin, declarationBegin, output);")
        elif type(inst) is RememberDeclarationBeginInstruction:
            line("declarationBegin = output.tokenBuffer.currentToken();")
        elif type(inst) is EndDeclarationInstruction:
            line("endDeclaration(output);")
        elif type(inst) is EmitCallTokenInstruction:
            line("argumentPosition = emitCallToken(argumentPosition, " + inst.tokenKindExpr + ", tokBegin, output);")
        elif type(inst) is CallArgumentInstruction:
            nameExpr = "identifier_t()"
            if not inst.nameExpr is None:
                nameExpr = inst.nameExpr
            line("argumentPosition = addCallArgument(argumentPosition, " + nameExpr + ", output);")
        elif type(inst) is UpdateCallArgumentInstruction:
            nameExpr = "identifier_t()"
            if not inst.nameExpr is None:
                nameExpr = inst.nameExpr
            line("updateCallArgument(argumentPosition, " + nameExpr + ", output);")
        elif type(inst) is EndCallInstruction:
            line("argumentPosition = endCall(argumentPosition, output);")
        elif type(inst) is SetGlobalKindInstruction:
            line("setGlobalKind(output, " + inst.globalKindExpr + ");")
        elif type(inst) is LexTokenInstruction:
            line("return LexerToken::" + case.cppName() + ";")
            afterJumpInstruction = True
        else:
            raise Exception("invalid instruction \"" + inst.format() + "\"")
    return afterJumpInstruction

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
findState("start").origins.append(NextInstruction("start"))


def onlyUsedAsThen(state):
    if [o for o in state.origins if type(o) is NextInstruction]:
        return False
    return True

line("switch (parseState) {")
for state in states:
    line("case State::" + stateCppName(state.name) + ":")
    with indent():
        if onlyUsedAsThen(state) or state.name == "error":
            line("VERIFY_NOT_REACHED();")
        else:
            line("goto " + state.name + "$no_emit;")
line("default:")
with indent():
    line("VERIFY_NOT_REACHED();")
line("}")

assert onlyUsedAsThen(errorState)
for state in nonErrorStates:
    if len(state.origins) == 0:
        raise Exception("unused state '" + state.name + "'")

    line("// " + state.kind + " " + state.name)
    withEmitLabelUsed = [o for o in state.origins if type(o) is NextInstruction and o.carriesEmitToken]
    if withEmitLabelUsed:
        labelLine(state.name + "$with_emit:")
        emitCarriedToken()
    noEmitLabelUsed = not onlyUsedAsThen(state)
    if noEmitLabelUsed:
        labelLine(state.name + "$no_emit:")
    if noEmitLabelUsed or withEmitLabelUsed:
        if state.dispatchCase():
            assert not [o for o in state.origins if not type(o) is NextInstruction]
            dispatchClosed = generateCaseBody(state.dispatchCase())
            if dispatchClosed:
                lineNoIndent()
                continue
        rememberState(state)
        line("if (tokenLimit == 0)")
        with indent():
            line("goto reached_token_limit;")
        line("tokenLimit -= 1;")
        if state.kind == "SwitchState":
            labelLine(state.name + "$retry:")
            line("tokEnd = skipWhitespace(tokEnd);")
            line("tokBegin = tokEnd;")
        else:
            inlineTokenAdvancer()
        if generateStateDebug:
            line("dbgln(\"" + state.name + ": {}\", *tokEnd);")
    labelLine("LABEL_MAYBE_UNUSED " + state.name + "$as_then:")

    generateState(state)

    lineNoIndent()

generatedParserLines = generatedLines
generatedLines = []

allCases  = [PunctuationCase(p, [LexTokenInstruction()]) for p in punctuations]
allCases += [KeywordCase(k, [LexTokenInstruction()]) for k in keywords]
allCases += [KeywordCase(k, [LexTokenInstruction()]) for k in specialIdentifiers]
allCases += [IdentifierCase([LexTokenInstruction()]), CharacterLiteralCase([LexTokenInstruction()]), NumericLiteralCase([LexTokenInstruction()])]
allCases += [EndCase([LexTokenInstruction()])]
lexState = State("SwitchState", "lex", "error", [], allCases)
lexState.origins = [NextInstruction("blub")]
labelLine("lex$retry:")
line("tokEnd = skipWhitespace(tokEnd);")
line("tokBegin = tokEnd;")
generateState(lexState)
generatedLexerLines = generatedLines

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
        for generatedLine in generatedParserLines:
            if len(generatedLine) > 0:
                outputLines.append(' ' * extraIndent + generatedLine + lineEnding)
            else:
                outputLines.append(lineEnding)
    elif strippedLine.startswith("// GENERATED LEXER CODE HERE"):
        extraIndent = len(inputLine) - len(strippedLine) - 4
        lineEnding = inputLine[len(inputLine.rstrip('\r\n')):]
        for generatedLine in generatedLexerLines:
            if len(generatedLine) > 0:
                outputLines.append(' ' * extraIndent + generatedLine + lineEnding)
            else:
                outputLines.append(lineEnding)
    else:
        outputLines.append(inputLine)

def writeTo(file, lines):
    try:
        with open(file, "r") as f:
            if f.readlines() == lines:
                return
    except:
        pass
    with open(file, "w") as f:
        f.writelines(lines)
writeTo(currentDir / "parse.cpp", outputLines)

# generate .h
generatedLines = []
outputIndentation = 0
lineNoIndent("#pragma once")
lineNoIndent()
lineNoIndent("#include <EnumTable.h>")
lineNoIndent("#include <parse/IdentifierTable.h>")
lineNoIndent()
line("namespace parse {")
lineNoIndent()

line("inline constexpr ConstWordStringTable words {")
with indent():
    for keyword in keywords:
        line("wordWithId(\"" + keyword + "\", KEYWORD_WORD_ID),")
    for identifier in specialIdentifiers:
        line("wordWithId(\"" + identifier + "\", SPECIAL_IDENTIFIER_WORD_ID),")
    for identifier in regularIdentifiers:
        line("wordInIdRange(\"" + identifier + "\", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),")
line("};")
line("inline constexpr Word generated_identifier = words[\"(generated_identifier)\"];")
lineNoIndent()

line("enum class LexerToken : uint8_t {")
with indent():
    for punc in punctuationTokens:
        line(punctuationCppName(punc) + ", // " + punc)
    line(characterLiteralCppName() + ",")
    line(numericLiteralCppName() + ",")
    line(identifierCppName() + ",")
    for keyword in keywords + specialIdentifiers:
        line(keywordCppName(keyword) + ", // " + keyword)
    line("EOS,")
    line("COUNT,")
    lineNoIndent()
    line("FirstPunctuation = " + punctuationCppName(punctuationTokens[0]) + ",")
    line("LastPunctuation = " + punctuationCppName(punctuationTokens[-1]) + ",")
    lineNoIndent()
    line("FirstKeyword = " + keywordCppName(keywords[0])+ ",")
    line("LastKeyword = " + keywordCppName(keywords[-1])+ ",")
    lineNoIndent()
    line("FirstSpecialIdentifier = " + keywordCppName(specialIdentifiers[0])+ ",")
    line("LastSpecialIdentifier = " + keywordCppName(specialIdentifiers[-1])+ ",")
    lineNoIndent()
    line("Invalid = 255")
line("};")
line("std::string_view nameString(LexerToken);")
line("constexpr bool isKeyword(LexerToken token) {")
with indent():
    line("return LexerToken::FirstKeyword <= token && token <= LexerToken::LastKeyword;")
line("}")
line("constexpr bool isSpecialIdentifier(LexerToken token) {")
with indent():
    line("return LexerToken::FirstSpecialIdentifier <= token && token <= LexerToken::LastSpecialIdentifier;")
line("}")
lineNoIndent()

line("inline constexpr EnumTable<LexerToken, std::string_view> fixedSpelling = {")
with indent():
    line("\"\",")
    line("{")
    with indent():
        for punc in punctuationTokens:
            line("{ LexerToken::" + punctuationCppName(punc) + ", \"" + punc + "\" },")
        for keyword in keywords + specialIdentifiers:
            line("{ LexerToken::" + keywordCppName(keyword) + ", \"" + keyword + "\" },")
    line("},")
line("};")
lineNoIndent()

line("enum class State : uint8_t {")
with indent():
    for state in states:
        line(stateCppName(state.name) + ",")
    line("COUNT,")
line("};")
line("std::string_view nameString(State);")
lineNoIndent()
line("std::span<const State> thenStates(State);")
line("std::span<const LexerToken> possibleTokens(State);")
lineNoIndent()
line("}")

outputLines = []
for generatedLine in generatedLines:
    outputLines.append(generatedLine + lineEnding)
writeTo(currentDir / "parse_gen.h", outputLines)

# generate .cpp
generatedLines = []
outputIndentation = 0
lineNoIndent("#include <parse/parse_gen.h>")
lineNoIndent()
lineNoIndent("#include <parse/TokenBuffer.h>")
lineNoIndent()
line("namespace parse {")
lineNoIndent()

line("std::string_view nameString(LexerToken token) {")
with indent():
    line("switch (token) {")
    for punc in punctuationTokens:
        line("case LexerToken::" + punctuationCppName(punc) + ":")
        with indent():
            line("return \"" + punctuationCppName(punc) + "\";")
    for keyword in keywords + specialIdentifiers:
        line("case LexerToken::" + keywordCppName(keyword) + ":")
        with indent():
            line("return \"" + keywordCppName(keyword) + "\";")
    line("case LexerToken::" + identifierCppName() + ":")
    with indent():
        line("return \"" + identifierCppName() + "\";")
    line("case LexerToken::" + characterLiteralCppName() + ":")
    with indent():
        line("return \"" + characterLiteralCppName() + "\";")
    line("case LexerToken::" + numericLiteralCppName() + ":")
    with indent():
        line("return \"" + numericLiteralCppName() + "\";")
    line("case LexerToken::EOS:")
    with indent():
        line("return \"EOS\";")
    line("case LexerToken::Invalid:")
    with indent():
        line("return \"Invalid\";")
    line("default:")
    with indent():
        line("VERIFY_NOT_REACHED();")
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
    line("default:")
    with indent():
        line("VERIFY_NOT_REACHED();")
    line("}")
line("}")
lineNoIndent()

line("std::span<const LexerToken> possibleTokens(State state) {")
with indent():
    line("switch (state) {")
    for state in nonErrorStates:
        line("case State::" + stateCppName(state.name) + ": {")
        with indent():
            caseNames = []
            for case in state.cases:
                if type(case) is ThenCase or type(case) is DispatchCase or type(case) is EndCase:
                    continue
                caseNames.append(case.cppName())
            if caseNames:
                line("static constexpr std::array r = { " + (", ".join(["LexerToken::" + c for c in caseNames])) + " };")
                line("return r;")
            else:
                line("return {};")
        line("}")
    line("default:")
    with indent():
        line("VERIFY_NOT_REACHED();")
    line("}")
line("}")
lineNoIndent()

line("std::span<const State> thenStates(State state) {")
with indent():
    line("switch (state) {")
    for state in states:
        line("case State::" + stateCppName(state.name) + ": {")
        with indent():
            thenStates = sorted(collectPossibleThenStates(state))
            if thenStates:
                line("static constexpr std::array r = { " + (", ".join(["State::" + stateCppName(s) for s in thenStates])) + " };")
                line("return r;")
            else:
                line("return {};")
        line("}")
    line("default:")
    with indent():
        line("VERIFY_NOT_REACHED();")
    line("}")
line("}")
lineNoIndent()

line("}")

outputLines = []
for generatedLine in generatedLines:
    outputLines.append(generatedLine + lineEnding)
writeTo(currentDir / "parse_gen.cpp", outputLines)

gperfFile = """
%{
#pragma once
#include <parse/parse_gen.h>
#include <cstring>
namespace parse {
%}

%language=C++
%7bit
%compare-lengths
%compare-strncmp
%readonly-tables
%enum
%define class-name KeywordTable
%define lookup-function-name get
%define constants-prefix KEYWORD_TABLE_
%define word-array-name KEYWORD_TABLE_ENTRIES
%define length-table-name KEYWORD_TABLE_LENGTHS
%global-table
%struct-type
%define slot-name string
%define initializer-suffix ,LexerToken::Identifier
struct KeywordTableEntry { const char* string; LexerToken token; };

%%
""" + lineEnding.join([keyword + ",LexerToken::" + keywordCppName(keyword) for keyword in keywords + specialIdentifiers]) + """
%%

}
"""
writeTo(currentDir / "keyword_table.gperf", gperfFile.splitlines(True))