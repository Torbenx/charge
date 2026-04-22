#include <parse/api.h>

#include <parse/Parser.h>
#include <sema/Context.h>

namespace parse {

static Error makeError(SavedParserState state) {
    SimpleParser parser;
    parser.restore(state);
    LexerToken token = parser.lexToken();
    return { std::move(state), token };
}

std::optional<Error> tryParse(sema::Context& context) {
    VERIFY(context.tokenBuffer.tokens.empty());
    Parser parser(context.tokenBuffer.source.data());
    parser.parse(context);
    if (parser.done())
        return std::nullopt;
    else
        return makeError(SimpleParser::saveStateOf(parser));
}

void parseOrThrow(sema::Context& context) {
    auto errorOpt = tryParse(context);
    if (errorOpt.has_value()) {
        std::string message = formatInternalErrorMessage(errorOpt.value(), context);
        throw ParseException(std::move(errorOpt.value()), std::move(message));
    }
}

std::vector<RecoveredError> recoverAndAnalyze(const SavedParserState& rootErrorState);

std::vector<RecoveredError> parseAndRecover(sema::Context& context) {
    VERIFY(context.tokenBuffer.tokens.empty());
    Parser parser(context.tokenBuffer.source.data());
    parser.parse(context);
    if (parser.done())
        return {};

    auto path = recoverAndAnalyze(SimpleParser::saveStateOf(parser));
    for (const auto& element : path) {
        VERIFY(SimpleParser::saveStateOf(parser) == element.errorState);
        VERIFY(parser.apply(context, element.recovery) == ReturnStatus::Ready);
        parser.parse(context);
    }
    VERIFY(parser.done());
    VERIFY(context.m_scopeStack.size() == 1);
    return path;
}

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
        return fmt::format("Invalid token '{}' for state '{}'",
            tokenDesc, nameString(error.errorState.state));
        break;
    case ReturnStatus::ScopeError:
        return fmt::format("Invalid scope '{}' while handling token '{}' in state '{}'",
            nameString(error.errorState.scopeBuffer.back()),
            tokenDesc, nameString(error.errorState.continueState));
        break;
    default:
        VERIFY_NOT_REACHED();
    }
}

std::string formatInternalErrorMessage(const Error& error, sema::Context& context) {
    SourceLocation location = context.tokenBuffer.findSourceLocation(error.errorState.sourcePosition);
    return fmt::format("Parse error on line {}: {} at \"{}\"",
        location.lineNumber(), detailedMessage(error),
        findSourceExcerpt(error.errorState.sourcePosition));
}

std::string formatInternalErrorMessage(const Error& error) {
    return fmt::format("Parse error: {} at \"{}\"",
        detailedMessage(error), findSourceExcerpt(error.errorState.sourcePosition));
}

const char* ParseException::what() const noexcept {
    if (m_message.empty())
        return "Parse error";
    else
        return m_message.data();
}

}