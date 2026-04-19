#include <parse/api.h>

#include <parse/Parser.h>
#include <sema/Context.h>

namespace parse {

void parseOrThrow(sema::Context& context) {
    VERIFY(context.tokenBuffer.tokens.empty());
    Parser parser(context.tokenBuffer.source.data());
    parser.parse(context);
    if (!parser.checkFinalState())
        throw ParseException();
}

const char* ParseException::what() const noexcept {
    return "A parse error occured";
}

}