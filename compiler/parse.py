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
    emitLine("if (tokEnd[1] == '\\n') {")
    with indent():
        emitLine("tokEnd += 2;")
        emitLine("continue;")
    emitLine("}")
    emitLine("tokEnd += 1;")
    emitLine("continue;")

def emitLineCommentHandler():
    emitLine("tokEnd = skipToEndOfLine(tokEnd);")
    emitLine("emitNode(NodeKind::LineComment, tokBegin, tokEnd, state, sourceBufferBegin);")
    emitLine("continue;")

def emitBlockCommentHandler():
    emitLine("tokEnd = skipToEndOfBlockComment(tokEnd);")
    emitLine("tokEnd += 2;")
    emitLine("emitNode(NodeKind::BlockComment, tokBegin, tokEnd, state, sourceBufferBegin);")
    emitLine("continue;")

def emitLinearIf(commonPrefix: str, handler):
    puncs = list(filter(lambda p: (p.startswith(commonPrefix)), Punctuation))
    possibleContinuations = set()
    exactMatch: Punctuation | None = None
    for p in puncs:
        if len(p) == len(commonPrefix):
            assert(exactMatch == None)
            exactMatch = p
        else:
            possibleContinuations.add(p[len(commonPrefix)])
    assert(exactMatch != None)

    if possibleContinuations:
        emitLine("char next = tokEnd[" + str(len(commonPrefix)) + "];")
    for character in sorted(possibleContinuations):
        emitLine("if (next == '" + character + "') {")
        with indent():
            emitLinearIf(commonPrefix + character, handler)
        emitLine("}")

    emitLine("tokEnd += " + str(len(exactMatch)) + ";")
    if exactMatch is Punctuation.SlashSlash:
        emitLineCommentHandler()
    elif exactMatch is Punctuation.SlashStar:
        emitBlockCommentHandler()
    else:
        handler.punctuation(exactMatch)

def emitCheckFor(punc, handler):
    puncs = list(filter(lambda p: (p.startswith(punc)), Punctuation))
    assert(len(puncs) > 0)
    emitLine("if (std::string_view(tokEnd, " + str(len(punc)) + ") == \"" + punc + "\") {")
    with indent():
        if len(puncs) == 1:
            assert(puncs[0] == punc)
            emitLine("tokEnd += " + str(len(punc)) + ";")
            handler()
        else:
            possibleContinuations = set()
            for p in puncs:
                if p != punc:
                    possibleContinuations.add(p[len(punc)])
            emitLine("char next = tokEnd[" + str(len(punc)) + "];")
            condition = ""
            for c in possibleContinuations:
                condition += " && next != '" + c + "'"
            condition = condition[4:]
            emitLine("if (" + condition + ") {")
            with indent():
                emitLine("tokEnd += " + str(len(punc)) + ";")
                handler()
            emitLine("}")
    emitLine("}")

def emitInlineTokenAdvancer():
    emitLine("tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);")


def emitSwitch(stateName, handler):
    emitLabelLine(stateName + ":")

    # store
    emitLine("emitNode(tokKind, tokBegin, tokEnd, state, sourceBufferBegin);")

    emitLabelLine(stateName + "_continue:")
    emitLine("for (;;) {")
    with indent():
        emitLine("tokEnd = skipWhitespace(tokEnd);")
        emitLine("tokBegin = tokEnd;")
        emitLine("tokKind = (NodeKind)0;")
        emitLine("data1 = 0;")
        emitLabelLine(stateName + "_dispatch:")
        emitLine("fmt::println(\"" + stateName + ": {}\", tokEnd[0]);")
        emitLine("switch (tokEnd[0]) {")

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
            emitLine("Word word;")
            emitLine("tokEnd = readWord(tokEnd, word, state.wordTable);")
            handler.word()
        emitLine("}")

        # default
        emitLine("default: {")
        with indent():
            emitLine("if (tokEnd[0] == '\\0' && tokEnd == state.sourceBufferEnd) {")
            with indent():
                emitLine("return reachedEOS(state);")
            emitLine("}")
            emitLine("TODO();")
        emitLine("}")

        emitLine("} // switch")

        emitLine("VERIFY_NOT_REACHED();")
    emitLine("} // retry-loop")

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
            emitLine("goto begin_argument_scope;")
        else:
            emitLine("TODO();")

    def word(self):
        emitLine("if (word == words[\"if\"]) {")
        with indent():
            emitLine("beginScope(ScopeKind::IfExpr, state.scopes);")
            emitLine("continue;")
        emitLine("}")
        emitLine("tokKind = NodeKind::IdentifierExpr;")
        emitLine("goto after_expression;")

class StatementHandler(ExpressionHandler):
    def punctuation(self, punc: Punctuation):
        if punc is Punctuation.RightBrace:
            emitLine("endScope(ScopeKind::CompoundStmt, state.scopes);")
            emitLine("tokKind = NodeKind::EmptyNode;")
            emitLine("goto statement;")
        else:
            super().punctuation(punc)

    def word(self):
        emitLine("if (word == words[\"if\"]) {")
        with indent():
            emitLine("beginScope(ScopeKind::IfExprOrStmt, state.scopes);")
            emitLine("goto expression_continue;")
        emitLine("}")
        super().word()

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
            emitLine("if (isWordFirstCharacter(tokEnd[0])) {")
            with indent():
                emitLine("Word word;")
                emitLine("tokEnd = readWord(tokEnd, word, state.wordTable);")
                emitLine("tokKind = NodeKind::" + ("Member" if punc is Punctuation.Point else "Static") + "AccessExpr;")
                emitLine("goto after_expression;")
            emitLine("}")
            emitLine("TODO();")
        elif punc is Punctuation.LeftParen:
            emitLine("tokKind = NodeKind::CallExpr;")
            emitLine("data1 = (size_t)ScopeKind::Paren;")
            emitLine("goto begin_argument_scope;")
        elif punc is Punctuation.LeftSquare:
            emitLine("tokKind = NodeKind::IndexExpr;")
            emitLine("data1 = (size_t)ScopeKind::Square;")
            emitLine("goto begin_argument_scope;")
        elif punc is Punctuation.LeftBrace:
            emitLine("tokKind = NodeKind::Parameterize;")
            emitLine("data1 = (size_t)ScopeKind::Brace;")
            emitLine("goto begin_argument_scope;")
        elif punc is Punctuation.RightParen:
            emitLine("endScope(ScopeKind::Paren, state.scopes);")
            emitLine("tokKind = NodeKind::EmptyNode;")
            emitLine("goto after_expression;")
        elif punc is Punctuation.RightSquare:
            emitLine("endScope(ScopeKind::Square, state.scopes);")
            emitLine("tokKind = NodeKind::EmptyNode;")
            emitLine("goto after_expression;")
        elif punc is Punctuation.RightBrace:
            emitLine("endScope(ScopeKind::Brace, state.scopes);")
            emitLine("tokKind = NodeKind::EmptyNode;")
            emitLine("goto after_expression;")
        elif punc is Punctuation.Comma:
            emitInlineTokenAdvancer()

            # comma-else
            emitLine("if (std::string_view(tokEnd, 4) == \"else\" && !isWordBulkCharacter(tokEnd[4])) {")
            with indent():
                emitLine("tokEnd += 4;")
                emitInlineTokenAdvancer()
                emitLine("if (std::string_view(tokEnd, 2) == \"=>\") {")
                with indent():
                    emitLine("tokEnd += 2;")
                    emitLine("tokKind = NodeKind::CommaElseExpr;")
                    emitLine("goto expression;")
                emitLine("}")
                emitLine("TODO();")
            emitLine("}")
            emitLine("if (std::string_view(tokEnd, 4) == \"elif\" && !isWordBulkCharacter(tokEnd[4])) {")
            with indent():
                emitLine("TODO();")
            emitLine("}")

            # lists
            emitLine("if (!state.scopes.empty()) {")
            with indent():
                emitLine("auto scopeKind = state.scopes.back();")
                emitLine("if (isBracketScope(scopeKind)) {")
                with indent():
                    emitLine("if (tokEnd[0] == scopeKindToRightBracket(scopeKind)) {")
                    with indent():
                        emitLine("endScope(scopeKind, state.scopes);")
                        emitLine("tokEnd += 1;")
                        emitLine("tokKind = NodeKind::EmptyNode;")
                        emitLine("goto after_expression;")
                    emitLine("}")
                    emitLine("if (isWordFirstCharacter(tokEnd[0])) {")
                    with indent():
                        emitLine("goto check_for_designated_argument;")
                    emitLine("}")
                    emitLine("goto expression_dispatch;")
                emitLine("}")
            emitLine("}")
            emitLine("TODO();")
        elif punc is Punctuation.SemiColon:
            emitLine("tokKind = NodeKind::ExpressionStmt;")
            emitLine("goto statement;")
        elif punc is Punctuation.FatArrow:
            emitLine("auto scopeKind = state.scopes.back();")
            emitLine("if (scopeKind == ScopeKind::IfExpr || scopeKind == ScopeKind::IfExprOrStmt) {")
            with indent():
                emitLine("endScope(scopeKind, state.scopes);")
                emitLine("tokKind = NodeKind::IfExpr;")
                emitLine("goto expression;")
            emitLine("}")
            emitLine("TODO();")
        elif punc is Punctuation.Colon:
            emitLine("auto scopeKind = state.scopes.back();")
            emitLine("if (scopeKind == ScopeKind::IfExprOrStmt) {")
            with indent():
                emitLine("endScope(scopeKind, state.scopes);")
                emitLine("tokKind = NodeKind::IfStmt;")
                emitLine("goto single_or_compound_statement;")
            emitLine("}")
            emitLine("TODO();")
        else:
            emitLine("TODO();")

    def word(self):
        emitLine("TODO();")

# generate
with indent():
    # check_for_designated_argument
    emitLabelLine("check_for_designated_argument : {")
    emitLine("Word word;")
    emitLine("tokEnd = readWord(tokEnd, word, state.wordTable);")
    emitLine("auto savedBegin = tokBegin;")
    emitLine("auto savedEnd = tokEnd;")
    emitInlineTokenAdvancer()
    def designatedArgument():
        emitLine("tokKind = NodeKind::DesignateArgument;")
        emitLine("goto expression;")
    emitCheckFor(Punctuation.Equal, designatedArgument)
    emitLine("emitNode(NodeKind::IdentifierExpr, savedBegin, savedEnd, state, sourceBufferBegin);")
    emitLine("goto after_expression_dispatch;")
    emitLabelLine("}")

    # begin_argument_scope
    emitLabelLine("begin_argument_scope : {")
    emitLine("emitNode(tokKind, tokBegin, tokEnd, state, sourceBufferBegin);")
    emitLine("ScopeKind scopeKind = (ScopeKind)data1;")
    emitInlineTokenAdvancer()
    emitLine("if (tokEnd[0] == scopeKindToRightBracket(scopeKind)) {")
    with indent():
        emitLine("tokEnd += 1;")
        emitLine("tokKind = NodeKind::EmptyNode;")
        emitLine("goto after_expression;")
    emitLine("}")
    emitLine("beginScope(scopeKind, state.scopes);")
    emitLine("if (isWordFirstCharacter(tokEnd[0])) {")
    with indent():
        emitLine("goto check_for_designated_argument;")
    emitLine("}")
    emitLine("goto expression_dispatch;")
    emitLabelLine("}")

    # single_or_compound_statment
    emitLabelLine("single_or_compound_statement : {")
    emitLine("emitNode(tokKind, tokBegin, tokEnd, state, sourceBufferBegin);")
    emitInlineTokenAdvancer()
    def compoundStatement():
        emitLine("beginScope(ScopeKind::CompoundStmt, state.scopes);")
        emitLine("tokKind = NodeKind::CompoundStmt;")
        emitLine("goto statement;")
    emitCheckFor(Punctuation.LeftBrace, compoundStatement)
    emitLine("goto statement_dispatch;")
    emitLabelLine("}")

    emitLineNoIndent()

    emitSwitch("statement", StatementHandler())
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