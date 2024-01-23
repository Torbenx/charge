import enum
import string
import pathlib

class Punctuation(enum.StrEnum):
    LeftParen = "("
    RightParen = ")"
    LeftSquare = "["
    RightSquare = "]"
    LeftBrace = "{"
    RightBrace = "}"
    Question = "?"
    Exclaim = "!"
    Tilde = "~"
    PlusPlus = "++"
    MinusMinus = "--"
    Plus = "+"
    Minus = "-"
    Star = "*"
    Amp = "&"
    Hat = "^"
    Vert = "|"
    Slash = "/"
    Percent = "%"
    LessLess = "<<"
    GreaterGreater = ">>"
    AmpAmp = "&&"
    VertVert = "||"
    ExclaimEqual = "!="
    EqualEqual = "=="
    Less = "<"
    LessEqual = "<="
    Greater = ">"
    GreaterEqual = ">="
    Equal = "="
    PlusEqual = "+="
    MinusEqual = "-="
    StarEqual = "*="
    AmpEqual = "&="
    HatEqual = "^="
    VertEqual = "|="
    SlashEqual = "/="
    PercentEqual = "%="
    LessLessEqual = "<<="
    GreaterGreaterEqual = ">>="
    AmpAmpEqual = "&&="
    VertVertEqual = "||="
    Comma = ","
    Point = "."
    Colon = ":"
    ColonColon = "::"
    SemiColon = ";"
    FatArrow = "=>"
    DoubleArrow = "<=>"
    Arrow = "->"
    SlashSlash = "//"
    SlashStar = "/*"


outputIndentation = 0
generatedLines = []

def emitLine(line: str = ""):
    generatedLines.append('    ' * outputIndentation + line)

def emitLabelLine(line: str):
    generatedLines.append('    ' * (outputIndentation - 1) + line)

def emitLineNoIndent(line: str = ""):
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

def emitLineFeedHandler():
    emitLine("tokEnd += 1;")
    emitLine("continue;")

def emitCarriageReturnHandler():
    emitLine("if (sourceBuffer[tokEnd + 1] == '\\n') {")
    with indent():
        emitLine("tokEnd += 2;")
        emitLine("continue;")
    emitLine("}")
    emitLine("tokEnd += 1;")
    emitLine("continue;")

def emitLineCommentHandler():
    emitLine("tokEnd += 2;")
    emitLine("skipToEndOfLine();")
    emitLine("nodes.push_back({ NodeKind::LineComment, (uint32_t)tokBegin, (uint32_t)tokEnd });")
    emitLine("continue;")

def emitBlockCommentHandler():
    emitLine("tokEnd += 2;")
    emitLine("skipToEndOfBlockComment();")
    emitLine("tokEnd += 2;")
    emitLine("nodes.push_back({ NodeKind::BlockComment, (uint32_t)tokBegin, (uint32_t)tokEnd });")
    emitLine("continue;")

def emitInlineTokenAdvancer():
    with RetryLoop():
        emitLine("if (sourceBuffer[tokEnd] == '/') {")
        with indent():
            emitLine("if (sourceBuffer[tokEnd + 1] == '/') {")
            with indent():
                emitLineCommentHandler()
            emitLine("}")
            emitLine("if (sourceBuffer[tokEnd + 1] == '*') {")
            with indent():
                emitBlockCommentHandler()
            emitLine("}")
        emitLine("}")
        emitLine("if (sourceBuffer[tokEnd] == '\\r') {")
        with indent():
            emitLineFeedHandler()
        emitLine("}")
        emitLine("if (sourceBuffer[tokEnd] == '\\n') {")
        with indent():
            emitCarriageReturnHandler()
        emitLine("}")
        emitLine("break;")

def emitLinearIf(commonPrefix: str, handler):
    puncs = list(filter(lambda p: (p.startswith(commonPrefix)), Punctuation))
    possibleContinuations = set()
    exactMatch: Punctuation | None = None
    for p in puncs:
        assert(p.startswith(commonPrefix))
        if len(p) == len(commonPrefix):
            assert(exactMatch == None)
            exactMatch = p
        else:
            possibleContinuations.add(p[len(commonPrefix)])
    assert(exactMatch != None)

    if possibleContinuations:
        emitLine("char next = sourceBuffer[tokEnd + " + str(len(commonPrefix)) + "];")
    for character in sorted(possibleContinuations):
        emitLine("if (next == '" + character + "') {")
        with indent():
            emitLinearIf(commonPrefix + character, handler)
        emitLine("}")

    if exactMatch is Punctuation.SlashSlash:
        emitLineCommentHandler()
    elif exactMatch is Punctuation.SlashStar:
        emitBlockCommentHandler()
    else:
        emitLine("tokEnd += " + str(len(exactMatch)) + ";")
        handler.punctuation(exactMatch)

class RetryLoop(IndentHelper):
    def __enter__(self):
        emitLine("for (;;) {")
        super().__enter__()
        emitLine("skipWhitespace();")
        emitLine("tokBegin = tokEnd;")
        emitLine("tokKind = (NodeKind)0;")
        emitLine("data1 = 0;")

    def __exit__(self, *args):
        super().__exit__(*args)
        emitLine("} // retry-loop")


def emitSwitch(stateName, handler):
    emitLabelLine(stateName + ":")

    # store
    emitLine("nodes.push_back({ tokKind, (uint32_t)tokBegin, (uint32_t)tokEnd });")

    with RetryLoop():
        emitLabelLine(stateName + "_dispatch:")
        emitLine("fmt::println(\"" + stateName + ": {}\", sourceBuffer.substr(tokEnd, 1));")
        emitLine("switch (sourceBuffer[tokEnd]) {")

        # newline
        emitLine("case '\\n': {")
        with indent():
            emitLineFeedHandler()
        emitLine("}")
        emitLine("case '\\r': {")
        with indent():
            emitCarriageReturnHandler()
        emitLine("}")

        # punctuations
        firstCharacters = set()
        for punc in Punctuation:
            firstCharacters.add(punc[0])
        for character in sorted(firstCharacters):
            emitLine("case '" + character + "': {")
            with indent():
                emitLinearIf(str(character), handler)
            emitLine("}")

        # word
        for character in list(string.ascii_lowercase) + list(string.ascii_uppercase):
            emitLine("case '" + character + "':")
        emitLine("case '#':")
        emitLine("case '$':")
        emitLine("case '_': {")
        with indent():
            emitLine("auto word = readWord();")
            handler.word()
        emitLine("}")

        # default
        emitLine("default: {")
        with indent():
            emitLine("if (sourceBuffer[tokEnd] == '\\0' && tokEnd == (int_t)sourceBuffer.length()) {")
            with indent():
                emitLine("return reachedEOS();")
            emitLine("}")
            emitLine("TODO();")
        emitLine("}")

        emitLine("} // switch")

        emitLine("VERIFY_NOT_REACHED();")

class ExpressionHandler:
    def punctuation(self, punc: Punctuation):
        prefixOps = {
            Punctuation.Exclaim: "LogicalNotExpr",
            Punctuation.Tilde: "BitwiseNotExpr",
            Punctuation.Plus: "PlusExpr",
            Punctuation.Minus: "NegateExpr",
            Punctuation.PlusPlus: "PreIncrementExpr",
            Punctuation.MinusMinus: "PreDecrementExpr",
            Punctuation.Star: "DereferenceExpr",
        }
        if punc in prefixOps:
            emitLine("tokKind = NodeKind::" + prefixOps[punc] + ";")
            emitLine("goto expression;")
        elif punc is Punctuation.LeftParen:
            emitLine("tokKind = NodeKind::ParenthesizedExpr;")
            emitLine("data1 = (size_t)ScopeKind::Paren;")
            emitLine("goto immediate_right_bracket_or_expression;")
        else:
            emitLine("TODO();")

    def word(self):
        emitLine("if (word == words[\"if\"]) {")
        with indent():
            emitLine("beginScope(ScopeKind::IfExpr);")
            emitLine("continue;")
        emitLine("}")
        emitLine("tokKind = NodeKind::IdentifierExpr;")
        emitLine("goto after_expression;")

class AfterExpressionHandler:
    def punctuation(self, punc: Punctuation):
        postfixOps = {
            Punctuation.PlusPlus: "PostIncrementExpr",
            Punctuation.MinusMinus: "PostDecrementExpr",
        }
        binaryOps = {
            Punctuation.Plus: "AdditionExpr",
            Punctuation.Minus: "SubtractionExpr",
            Punctuation.Star: "MultiplyExpr",
            Punctuation.Amp: "BitwiseAndExpr",
            Punctuation.Hat: "BitwiseXorExpr",
            Punctuation.Vert: "BitwiseOrExpr",
            Punctuation.Slash: "DivideExpr",
            Punctuation.Percent: "RemainderExpr",
            Punctuation.LessLess: "ShiftLeftExpr",
            Punctuation.GreaterGreater: "ShiftRightExpr",
            Punctuation.AmpAmp: "LogicalAndExpr",
            Punctuation.VertVert: "LogicalOrExpr",
            Punctuation.ExclaimEqual: "CompareNotEqualExpr",
            Punctuation.EqualEqual: "CompareEqualExpr",
            Punctuation.Less: "CompareLessExpr",
            Punctuation.LessEqual: "CompareLessEqualExpr",
            Punctuation.Greater: "CompareGreaterExpr",
            Punctuation.GreaterEqual: "CompareGreaterEqualExpr",
        }
        if punc in postfixOps:
            emitLine("tokKind = NodeKind::" + postfixOps[punc] + ";")
            emitLine("goto after_expression;")
        elif punc in binaryOps:
            emitLine("tokKind = NodeKind::" + binaryOps[punc] + ";")
            emitLine("goto expression;")
        elif punc is Punctuation.Point or punc is Punctuation.ColonColon:
            emitInlineTokenAdvancer()
            emitLine("if (isWordFirstCharacter(sourceBuffer[tokEnd])) {")
            with indent():
                emitLine("auto word = readWord();")
                emitLine("tokKind = NodeKind::" + ("Member" if punc is Punctuation.Point else "Static") + "AccessExpr;")
                emitLine("goto after_expression;")
            emitLine("}")
            emitLine("TODO();")
        elif punc is Punctuation.LeftParen:
            emitLine("tokKind = NodeKind::CallExpr;")
            emitLine("data1 = (size_t)ScopeKind::Paren;")
            emitLine("goto immediate_right_bracket_or_expression;")
        elif punc is Punctuation.LeftSquare:
            emitLine("tokKind = NodeKind::IndexExpr;")
            emitLine("data1 = (size_t)ScopeKind::Square;")
            emitLine("goto immediate_right_bracket_or_expression;")
        elif punc is Punctuation.LeftBrace:
            emitLine("tokKind = NodeKind::Parameterize;")
            emitLine("data1 = (size_t)ScopeKind::Brace;")
            emitLine("goto immediate_right_bracket_or_expression;")
        elif punc is Punctuation.RightParen:
            emitLine("endScope(ScopeKind::Paren);")
            emitLine("tokKind = NodeKind::EmptyNode;")
            emitLine("goto after_expression;")
        elif punc is Punctuation.RightSquare:
            emitLine("endScope(ScopeKind::Square);")
            emitLine("tokKind = NodeKind::EmptyNode;")
            emitLine("goto after_expression;")
        elif punc is Punctuation.RightBrace:
            emitLine("endScope(ScopeKind::Brace);")
            emitLine("tokKind = NodeKind::EmptyNode;")
            emitLine("goto after_expression;")
        elif punc is Punctuation.Comma:
            emitInlineTokenAdvancer()

            # comma-else
            emitLine("if (sourceBuffer.substr(tokEnd, 4) == \"else\" && !isWordBulkCharacter(sourceBuffer[tokEnd + 4])) {")
            with indent():
                emitLine("tokEnd += 4;")
                emitInlineTokenAdvancer()
                emitLine("if (sourceBuffer.substr(tokEnd, 2) == \"=>\") {")
                with indent():
                    emitLine("tokEnd += 2;")
                    emitLine("tokKind = NodeKind::CommaElseExpr;")
                    emitLine("goto expression;")
                emitLine("}")
                emitLine("TODO();")
            emitLine("}")
            emitLine("if (sourceBuffer.substr(tokEnd, 4) == \"elif\" && !isWordBulkCharacter(sourceBuffer[tokEnd + 4])) {")
            with indent():
                emitLine("TODO();")
            emitLine("}")

            # lists
            emitLine("if (!scopes.empty()) {")
            with indent():
                emitLine("auto scopeKind = scopes.back().kind;")
                emitLine("if (isBracketScope(scopeKind)) {")
                with indent():
                    emitLine("if (sourceBuffer[tokEnd] == scopeKindToRightBracket(scopeKind)) {")
                    with indent():
                        emitLine("endScope(scopeKind);")
                        emitLine("tokEnd += 1;")
                        emitLine("tokKind = NodeKind::EmptyNode;")
                        emitLine("goto after_expression;")
                    emitLine("}")
                    emitLine("goto expression_dispatch;")
                emitLine("}")
            emitLine("}")
            emitLine("TODO();")
        elif punc is Punctuation.SemiColon:
            emitLine("tokKind = NodeKind::ExpressionStmt;")
            emitLine("goto expression;")
        elif punc is Punctuation.FatArrow:
            emitLine("endScope(ScopeKind::IfExpr);")
            emitLine("tokKind = NodeKind::IfExpr;")
            emitLine("goto expression;")
        else:
            emitLine("TODO();")

    def word(self):
        emitLine("TODO();")

# generate
with indent():
    emitLabelLine("immediate_right_bracket_or_expression : {")
    emitLine("nodes.push_back({ tokKind, (uint32_t)tokBegin, (uint32_t)tokEnd });")
    emitLine("ScopeKind scopeKind = (ScopeKind)data1;")
    emitInlineTokenAdvancer()
    emitLine("if (sourceBuffer[tokEnd] == scopeKindToRightBracket(scopeKind)) {")
    with indent():
        emitLine("tokEnd += 1;")
        emitLine("tokKind = NodeKind::EmptyNode;")
        emitLine("goto after_expression;")
    emitLine("}")
    emitLine("beginScope(scopeKind);")
    emitLine("goto expression_dispatch;")
    emitLabelLine("}")

    emitLineNoIndent()

    emitSwitch("expression", ExpressionHandler())
    emitLineNoIndent()
    emitSwitch("after_expression", AfterExpressionHandler())

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