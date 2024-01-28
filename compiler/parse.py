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

keywords = [
    "if", "elif", "else", "match", "for", "while", "do", "return", "break", "continue", "loop", "guard", "try", "catch", "with", "analysis", "assert",
    "namespace", "struct", "trait", "object", "fn", "static",
    "template",
    "var", "let", "in", "inout", "out", "forward", "assign",
    "true", "false",
]

outputIndentation = 0
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

def lineFeedHandler():
    line("tokEnd += 1;")
    line("continue;")

def carriageReturnHandler():
    line("if (tokEnd[1] == '\\n') {")
    with indent():
        line("tokEnd += 2;")
        line("continue;")
    line("}")
    line("tokEnd += 1;")
    line("continue;")

def lineCommentHandler():
    line("tokEnd = skipToEndOfLine(tokEnd);")
    line("emitNode(NodeKind::LineComment, tokBegin, tokEnd, state, sourceBufferBegin);")
    line("continue;")

def blockCommentHandler():
    line("tokEnd = skipToEndOfBlockComment(tokEnd);")
    line("tokEnd += 2;")
    line("emitNode(NodeKind::BlockComment, tokBegin, tokEnd, state, sourceBufferBegin);")
    line("continue;")

def linearIf(commonPrefix: str, handler):
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
        line("char next = tokEnd[" + str(len(commonPrefix)) + "];")
    for character in sorted(possibleContinuations):
        line("if (next == '" + character + "') {")
        with indent():
            linearIf(commonPrefix + character, handler)
        line("}")

    line("tokEnd += " + str(len(exactMatch)) + ";")
    if exactMatch is Punctuation.SlashSlash:
        lineCommentHandler()
    elif exactMatch is Punctuation.SlashStar:
        blockCommentHandler()
    else:
        handler.punctuation(exactMatch)

def checkFor(punc, handler):
    puncs = list(filter(lambda p: (p.startswith(punc)), Punctuation))
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

class ErrorHandler:
    def punctuation(self, punc: Punctuation):
        line("TODO_ERROR(\"invalid token for state\");")

    def keyword(self, keyword: str):
        line("TODO_ERROR(\"invalid token for state\");")

    def identifier(self):
        line("TODO_ERROR(\"invalid token for state\");")

class InlineAdvancerCertificate:
    pass

def inlineTokenAdvancer():
    line("tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);")
    line("tokBegin = tokEnd;")
    return InlineAdvancerCertificate()

def inlineIdentifier():
    # TODO: Handle keywords
    line("tokEnd = skipToEndOfIdentifier(tokEnd);")

def pushScope(scopeKindExpr):
    line("scopePosition = pushScope(" + scopeKindExpr + ", scopePosition);")

def popScope(scopeKindExpr):
    line("scopePosition = popScope(" + scopeKindExpr + ", scopePosition);")

def peekScope(scopeKindVariableName):
    line("auto " + scopeKindVariableName + " = peekScope(scopePosition);")

def emitNode(nodeKindExpr, beginExpr, endExpr):
    line("emitNode(" + nodeKindExpr + ", " + beginExpr +", " + endExpr + ", state, sourceBufferBegin);")

def emitNodeFromLocals():
    emitNode("nodeKind", "tokBegin", "tokEnd")

def gotoStateAndSave(stateName, nodeKindExpr):
    if nodeKindExpr != "nodeKind":
        line("nodeKind = " + nodeKindExpr + ";")
    line("goto " + stateName + ";")

def gotoStateNoSave(stateName):
    line("goto " + stateName + "_continue;")

def gotoStateAlreadyAdvanced(stateName, advance):
    assert(type(advance) == InlineAdvancerCertificate)
    line("goto " + stateName + "_dispatch;")

def generateState(stateName, handler):
    labelLine(stateName + ":")

    # save
    emitNodeFromLocals()

    labelLine(stateName + "_continue:")
    line("for (;;) {")
    with indent():
        line("tokEnd = skipWhitespace(tokEnd);")
        line("tokBegin = tokEnd;")
        line("nodeKind = (NodeKind)0;")
        line("data1 = 0;")
        labelLine(stateName + "_dispatch:")
        line("fmt::println(\"" + stateName + ": {}\", tokEnd[0]);")
        line("switch (tokEnd[0]) {")

        # newline
        line("case '\\n': {")
        with indent():
            lineFeedHandler()
        line("}")
        line("case '\\r': {")
        with indent():
            carriageReturnHandler()
        line("}")

        # punctuations
        firstCharacters = set()
        for punc in Punctuation:
            firstCharacters.add(punc[0])
        for character in sorted(firstCharacters):
            line("case '" + character + "': {")
            with indent():
                linearIf(str(character), handler)
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
                            handler.keyword(keyword)
                        line("}")
                line("goto " + stateName + "_identifier_case;")
            line("}")

        for character in sorted(identifierBeginCharacters - keywordBeginCharacters):
            line("case '" + character + "':")
        line("case '#':")
        line("case '$':")
        line("case '_': {")
        with indent():
            line("goto " + stateName + "_identifier_case;")
        line("}")

        # default
        line("default: {")
        with indent():
            line("if (tokEnd[0] == '\\0' && tokEnd == state.sourceBufferEnd) {")
            with indent():
                line("return reachedEOS(state, scopePosition);")
            line("}")
            line("TODO_ERROR(\"invalid character\");")
        line("}")

        line("} // switch")

        line("VERIFY_NOT_REACHED();")

        labelLine(stateName + "_identifier_case:")
        line("tokEnd = skipToEndOfIdentifier(tokEnd);")
        handler.identifier()

    line("} // retry-loop")

class ExpressionHandler(ErrorHandler):
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
            gotoStateAndSave("expression", "NodeKind::" + prefixOps[punc])
        elif punc is Punctuation.LeftParen:
            line("nodeKind = NodeKind::ParenthesizedExpr;")
            line("data1 = (size_t)ScopeKind::Paren;")
            line("goto begin_argument_scope;")
        else:
            super().punctuation(punc)

    def keyword(self, keyword):
        if keyword == "if":
            pushScope("ScopeKind::IfExpr")
            gotoStateNoSave("expression")
        else:
            super().keyword(keyword)

    def identifier(self):
        gotoStateAndSave("after_expression", "NodeKind::IdentifierExpr")

class StatementHandler(ExpressionHandler):
    def punctuation(self, punc: Punctuation):
        if punc is Punctuation.RightBrace:
            popScope("ScopeKind::CompoundStmt")
            gotoStateAndSave("statement", "NodeKind::EmptyNode")
        else:
            super().punctuation(punc)

    def keyword(self, keyword):
        if keyword == "if":
            pushScope("ScopeKind::IfExprOrStmt")
            gotoStateNoSave("expression")
        else:
            super().keyword(keyword)

class AfterExpressionHandler(ErrorHandler):
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
            gotoStateAndSave("after_expression", "NodeKind::" + postfixOps[punc])
        elif punc in binaryOps:
            gotoStateAndSave("expression", "NodeKind::" + binaryOps[punc])
        elif punc is Punctuation.Point or punc is Punctuation.ColonColon:
            line("nodeKind = NodeKind::" + ("Member" if punc is Punctuation.Point else "Static") + "AccessExpr;")
            line("goto handle_access_punctuation;")
        elif punc is Punctuation.LeftParen:
            line("nodeKind = NodeKind::CallExpr;")
            line("data1 = (size_t)ScopeKind::Paren;")
            line("goto begin_argument_scope;")
        elif punc is Punctuation.LeftSquare:
            line("nodeKind = NodeKind::IndexExpr;")
            line("data1 = (size_t)ScopeKind::Square;")
            line("goto begin_argument_scope;")
        elif punc is Punctuation.LeftBrace:
            line("nodeKind = NodeKind::Parameterize;")
            line("data1 = (size_t)ScopeKind::Brace;")
            line("goto begin_argument_scope;")
        elif punc is Punctuation.RightParen:
            popScope("ScopeKind::Paren")
            gotoStateAndSave("after_expression", "NodeKind::EmptyNode")
        elif punc is Punctuation.RightSquare:
            popScope("ScopeKind::Square")
            gotoStateAndSave("after_expression", "NodeKind::EmptyNode")
        elif punc is Punctuation.RightBrace:
            popScope("ScopeKind::Brace")
            gotoStateAndSave("after_expression", "NodeKind::EmptyNode")
        elif punc is Punctuation.Comma:
            advance1 = inlineTokenAdvancer()

            # comma-else
            line("if (std::string_view(tokEnd, 4) == \"else\" && !isWordBulkCharacter(tokEnd[4])) {")
            with indent():
                line("tokEnd += 4;")
                advance2 = inlineTokenAdvancer()
                line("if (std::string_view(tokEnd, 2) == \"=>\") {")
                with indent():
                    line("tokEnd += 2;")
                    gotoStateAndSave("expression", "NodeKind::CommaElseExpr")
                line("}")
                line("TODO_ERROR(\"junk after comma-else\");")
            line("}")
            line("if (std::string_view(tokEnd, 4) == \"elif\" && !isWordBulkCharacter(tokEnd[4])) {")
            with indent():
                line("TODO_PARSE();")
            line("}")

            # lists
            peekScope("scopeKind")
            line("if (isBracketScope(scopeKind)) {")
            with indent():
                line("if (tokEnd[0] == scopeKindToRightBracket(scopeKind)) {")
                with indent():
                    popScope("scopeKind")
                    line("tokEnd += 1;")
                    gotoStateAndSave("after_expression", "NodeKind::EmptyNode")
                line("}")
                line("if (isWordFirstCharacter(tokEnd[0])) {")
                with indent():
                    line("goto check_for_designated_argument;")
                line("}")
                gotoStateAlreadyAdvanced("expression", advance1)
            line("}")
            line("TODO_ERROR(\"invalid scope for comma\");")
        elif punc is Punctuation.SemiColon:
            gotoStateAndSave("statement", "NodeKind::ExpressionStmt")
        elif punc is Punctuation.FatArrow:
            peekScope("scopeKind")
            line("if (scopeKind == ScopeKind::IfExpr || scopeKind == ScopeKind::IfExprOrStmt) {")
            with indent():
                popScope("scopeKind")
                gotoStateAndSave("expression", "NodeKind::IfExpr")
            line("}")
            line("TODO_ERROR(\"invalid scope for fat-arrow\");")
        elif punc is Punctuation.Colon:
            peekScope("scopeKind")
            line("if (scopeKind == ScopeKind::IfExprOrStmt) {")
            with indent():
                popScope("scopeKind")
                line("nodeKind = NodeKind::IfStmt;")
                line("goto single_or_compound_statement;")
            line("}")
            line("TODO_ERROR(\"invalid scope for colon\");")
        else:
            super().punctuation(punc)

# generate
with indent():
    # check_for_designated_argument
    labelLine("check_for_designated_argument : {")
    inlineIdentifier()
    line("auto savedBegin = tokBegin;")
    line("auto savedEnd = tokEnd;")
    advance = inlineTokenAdvancer()
    def designatedArgument():
        gotoStateAndSave("expression", "NodeKind::DesignateArgument")
    checkFor(Punctuation.Equal, designatedArgument)
    emitNode("NodeKind::IdentifierExpr", "savedBegin", "savedEnd")
    gotoStateAlreadyAdvanced("after_expression", advance)
    labelLine("}")

    # begin_argument_scope
    labelLine("begin_argument_scope : {")
    emitNodeFromLocals()
    line("ScopeKind scopeKind = (ScopeKind)data1;")
    advance = inlineTokenAdvancer()
    line("if (tokEnd[0] == scopeKindToRightBracket(scopeKind)) {")
    with indent():
        line("tokEnd += 1;")
        gotoStateAndSave("after_expression", "NodeKind::EmptyNode")
    line("}")
    pushScope("scopeKind")
    line("if (isWordFirstCharacter(tokEnd[0])) {")
    with indent():
        line("goto check_for_designated_argument;")
    line("}")
    gotoStateAlreadyAdvanced("expression", advance)
    labelLine("}")

    # single_or_compound_statment
    labelLine("single_or_compound_statement : {")
    emitNodeFromLocals()
    advance = inlineTokenAdvancer()
    def compoundStatement():
        pushScope("ScopeKind::CompoundStmt")
        gotoStateAndSave("statement", "NodeKind::CompoundStmt")
    checkFor(Punctuation.LeftBrace, compoundStatement)
    gotoStateAlreadyAdvanced("statement", advance)
    labelLine("}")

    # handle_access_punctuation
    labelLine("handle_access_punctuation : {")
    inlineTokenAdvancer()
    line("if (isWordFirstCharacter(tokEnd[0])) {")
    with indent():
        inlineIdentifier()
        gotoStateAndSave("after_expression", "nodeKind")
    line("}")
    line("TODO_ERROR(\"junk after access punctuation\");")
    labelLine("}")

    lineNoIndent()

    generateState("statement", StatementHandler())
    lineNoIndent()
    generateState("expression", ExpressionHandler())
    lineNoIndent()
    generateState("after_expression", AfterExpressionHandler())

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