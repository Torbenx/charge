#include <parse/api.h>

#include <parse/Parser.h>
#include <parse/Token.h>
#include <sema/Context.h>

namespace parse {

static std::string findSourceExcerpt(const char* begin) {
    const char* position = begin;
    const char* lastWhitespace = nullptr;
    for (;; position += 1) {
        if (position - begin >= 20)
            break;
        if ((unsigned char)*position < 0x20 || (unsigned char)*position >= 127)
            break;
        if (*position == ' ' || *position == '\t')
            lastWhitespace = position;
    }
    if (*position == '\n' || *position == '\r' || *position == '\0')
        return { begin, position };
    if (lastWhitespace != nullptr) {
        std::string result { begin, lastWhitespace };
        result += " ...";
        return result;
    }
    std::string result { begin, position };
    result += "...";
    return result;
}

static std::string detailedMessage(const Error& error) {
    std::string_view tokenDesc = fixedSpelling(error.errorToken);
    if (tokenDesc.empty())
        tokenDesc = nameString(error.errorToken);
    switch (error.errorState.status) {
    case ReturnStatus::UnhandledCase:
        return std::format("Invalid token '{}' for state '{}'",
            tokenDesc, nameString(error.errorState.state));
        break;
    case ReturnStatus::ScopeError:
        return std::format("Invalid scope '{}' while handling token '{}' in state '{}'",
            nameString(error.errorState.scopeBuffer.back()),
            tokenDesc, nameString(error.errorState.state));
        break;
    default:
        VERIFY_NOT_REACHED();
    }
}

Error::Error(SavedParserState errorStateArg)
    : errorState(std::move(errorStateArg)) {
    const char* pos = errorState.sourcePosition;
    errorToken = lexToken(pos);
}

static SavedParserState recoveryToErrorState(const SavedParserState& preRecoveryState) {
    SimpleParser parser;
    parser.restore(preRecoveryState);
    parser.parse(NoOutput(), 2);
    VERIFY(parser.error());
    return parser.save();
}

RecoveredError::RecoveredError(SavedParserState preRecoveryState, RecoveryInstructions recovery, bool unanimousAndIsolated)
    : Error(recoveryToErrorState(preRecoveryState))
    , preRecoveryState(std::move(preRecoveryState))
    , recovery(std::move(recovery))
    , unanimousAndIsolated(unanimousAndIsolated) {
}

std::optional<Error> tryParse(sema::Context& context) {
    VERIFY(context.tokenBuffer.tokens.empty());
    Parser parser(context.tokenBuffer.source.data());
    parser.parse(context);
    if (parser.done())
        return std::nullopt;
    else
        return Error(SimpleParser::saveStateOf(parser));
}

void parseOrThrow(sema::Context& context) {
    auto errorOpt = tryParse(context);
    if (errorOpt.has_value()) {
        std::string message = formatInternalErrorMessage(errorOpt.value(), context);
        throw ParseException(std::move(errorOpt.value()), std::move(message));
    }
}

namespace recovery {
    std::vector<RecoveredError> recoverAndAnalyze(std::string_view source, const SavedParserState& rootErrorState);
}

std::vector<RecoveredError> parseAndRecover(sema::Context& context) {
    VERIFY(context.tokenBuffer.tokens.empty());
    std::vector<RecoveredError> errors;
    {
        SimpleParser parser(context.tokenBuffer.source.data());
        parser.parse(NoOutput());
        if (!parser.done())
            errors = recovery::recoverAndAnalyze(context.tokenBuffer.source, parser.save());
    }

    Parser parser(context.tokenBuffer.source.data());
    for (const auto& element : errors) {
        parser.parse(context, (int_t)element.preRecoveryState.parsedTokens - parser.parsedTokens());
        VERIFY(SimpleParser::saveStateOf(parser) == element.preRecoveryState);
        VERIFY(parser.apply(context, element.recovery) == ReturnStatus::Ready);
    }
    parser.parse(context);
    VERIFY(parser.done());
    VERIFY(context.m_scopeStack.size() == 1);
    return errors;
}

std::string formatInternalErrorMessage(const Error& error, sema::Context& context) {
    SourceLocation location = context.tokenBuffer.findSourceLocation(error.errorState.sourcePosition);
    return std::format("Parse error on line {}: {} at \"{}\"",
        location.lineNumber(), detailedMessage(error),
        findSourceExcerpt(error.errorState.sourcePosition));
}

std::string formatInternalErrorMessage(const Error& error) {
    return std::format("Parse error: {} at \"{}\"",
        detailedMessage(error), findSourceExcerpt(error.errorState.sourcePosition));
}

const char* ParseException::what() const noexcept {
    if (m_message.empty())
        return "Parse error";
    else
        return m_message.data();
}

}