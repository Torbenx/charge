#include <verify/language/ParseContext.h>

#include <flat_set>
#include <format>
#include <ranges>

namespace verify::language {

struct FileScanner {
    //! A member or invariant of the type being scanned
    struct Definition {
        Word name;
        //! Set when the function was defined inline, empty for an alias
        std::optional<ir::FnHandle> fn;
    };

    //! A member or invariant naming an external function
    struct PendingAlias {
        GlobalKind kind;
        uint32_t index;
        Word function;
    };

    struct TypeScan {
        Word name;
        std::optional<TokenPosition> parameters;
        std::vector<Definition> invariants;
        std::vector<Definition> members;
        LookupTable<uint32_t> invariantIndices;
        std::vector<ir::Database::MemberUseInInvariant> usageMap;
        std::vector<PendingAlias> aliases;

        //! Cached here to avoid repeated allocation
        std::vector<bool> usedInvariants;
    };

    FileScanner(ParseContext& context, ScannedFile& scanned)
        : context(context), out(context.database), scanned(scanned) { }

    void scan(TokenStream& s);
    void scanFunction(TokenStream& s);
    void scanStruct(TokenStream& s);
    void scanTypeBody(TokenStream s, TypeScan& type);
    void scanInvariant(TokenStream& s, TypeScan& type);
    void scanMember(TokenStream& s, TypeScan& type);
    //! What follows the name of a member or invariant (and the invariants of a member)
    std::optional<ir::FnHandle> scanDefinitionBody(TokenStream& s, TypeScan& type, Word name, GlobalKind kind, uint32_t index);
    void addType(TokenStream& s, TypeScan& type);

    //! Reads a name of a definition that is not inside of a type
    Word readTopLevelName(TokenStream& s);
    //! Reads a name inside of a type, one beginning with ':' is prefixed with the name of the type
    Word readNameInType(TokenStream& s, const TypeScan& type);

    ir::FnHandle newFunction(Word name);
    void defineGlobal(TokenStream& s, Word name, GlobalName global);

    //! Moves past the parameter list, it is parsed in the second phase
    static void skipParameterList(TokenStream& s);
    //! Moves past the ':' and the body following it, it is parsed in the second phase
    static void skipBody(TokenStream& s);

    ParseContext& context;
    ParsedDatabase& out;
    ScannedFile& scanned;
};

void FileScanner::scan(TokenStream& s) {
    for (;;) {
        switch (s.tokKind()) {
        case TokenKind::EndScope:
            return;
        case TokenKind::ContinueScope:
            if (!s.tok().word().empty())
                s.error("Label is not inside of a body");
            s.advance();
            continue;
        case TokenKind::Identifier: {
            Word id = s.tok().word();
            if (id == words["fn"]) {
                scanFunction(s);
            } else if (id == words["struct"]) {
                scanStruct(s);
            } else {
                s.error("Expected 'fn' or 'struct'");
            }
            continue;
        }
        default:
            s.error("Expected definition");
        }
    }
}

void FileScanner::scanFunction(TokenStream& s) {
    VERIFY(s.tokKind() == TokenKind::Identifier);
    VERIFY(s.tok().word() == words["fn"]);
    s.advance();

    Word name = readTopLevelName(s);
    if (s.tokKind() != TokenKind::LeftParen)
        s.error("Expected '(' after function name");
    ir::FnHandle fn = newFunction(name);
    defineGlobal(s, name, GlobalName(fn));
    scanned.functions.push_back({ .fn = fn, .parameters = s.position(), .typeParameters = std::nullopt });
    skipParameterList(s);
    skipBody(s);
}

void FileScanner::scanStruct(TokenStream& s) {
    VERIFY(s.tokKind() == TokenKind::Identifier);
    VERIFY(s.tok().word() == words["struct"]);
    s.advance();

    TypeScan type;
    type.name = readTopLevelName(s);
    if (s.tokKind() == TokenKind::LeftParen) {
        type.parameters = s.position();
        skipParameterList(s);
    }
    if (s.tokKind() != TokenKind::Colon)
        s.error("Expected ':' after type header");
    s.advanceWithNoScopeChanges();
    if (s.tokKind() != TokenKind::BeginScope)
        s.error("Expected type body");
    scanTypeBody(s, type);

    addType(s, type);
}

void FileScanner::scanTypeBody(TokenStream s, TypeScan& type) {
    for (;;) {
        switch (s.tokKind()) {
        case TokenKind::EndScope:
            return;
        case TokenKind::ContinueScope:
            if (!s.tok().word().empty())
                s.error("Label is not inside of a body");
            s.advance();
            continue;
        case TokenKind::Identifier: {
            Word id = s.tok().word();
            if (id == words["invariant"]) {
                if (!type.members.empty())
                    s.error("Invariants must be declared before members");
                scanInvariant(s, type);
            } else if (id == words["member"]) {
                scanMember(s, type);
            } else {
                s.error("Expected 'invariant' or 'member'");
            }
            if (s.tokKind() != TokenKind::ContinueScope && s.tokKind() != TokenKind::EndScope)
                s.error("Unexpected token after definition");
            continue;
        }
        default:
            s.error("Expected 'invariant' or 'member'");
        }
    }
}

void FileScanner::scanInvariant(TokenStream& s, TypeScan& type) {
    VERIFY(s.tokKind() == TokenKind::Identifier);
    VERIFY(s.tok().word() == words["invariant"]);
    s.advance();

    Word name = readNameInType(s, type);
    uint32_t index = (uint32_t)type.invariants.size();
    if (type.invariantIndices.tryInsert(name, index).has_value())
        s.error(std::format("Global #{} is defined twice", context.wordTable.view(name)));
    type.invariants.push_back({ .name = name, .fn = std::nullopt });
    type.invariants[index].fn = scanDefinitionBody(s, type, name, GlobalKind::Invariant, index);
}

void FileScanner::scanMember(TokenStream& s, TypeScan& type) {
    VERIFY(s.tokKind() == TokenKind::Identifier);
    VERIFY(s.tok().word() == words["member"]);
    s.advance();

    Word name = readNameInType(s, type);
    uint32_t index = (uint32_t)type.members.size();
    type.members.push_back({ .name = name, .fn = std::nullopt });

    if (s.tokKind() == TokenKind::Identifier && s.tok().word() == words["invariants"]) {
        s.advance();
        type.usedInvariants.clear();
        type.usedInvariants.resize(type.invariants.size());
        for (;;) {
            Word invariantName = readNameInType(s, type);
            auto invariantIndexOpt = type.invariantIndices.get(invariantName);
            if (!invariantIndexOpt.has_value())
                s.error(std::format("#{} is not an invariant of the type", context.wordTable.view(invariantName)));
            uint32_t invariantIndex = invariantIndexOpt.value();
            if (type.usedInvariants[invariantIndex])
                s.error(std::format("Invariant #{} is listed twice", context.wordTable.view(invariantName)));
            type.usedInvariants[invariantIndex] = true;
            type.usageMap.push_back({ .memberIndex = index, .invariantIndex = invariantIndex });
            if (s.tokKind() != TokenKind::Comma)
                break;
            s.advance();
        }
    }

    type.members[index].fn = scanDefinitionBody(s, type, name, GlobalKind::Member, index);
}

std::optional<ir::FnHandle> FileScanner::scanDefinitionBody(TokenStream& s, TypeScan& type, Word name, GlobalKind kind, uint32_t index) {
    switch (s.tokKind()) {
    case TokenKind::LeftParen: {
        ir::FnHandle fn = newFunction(name);
        scanned.functions.push_back({ .fn = fn, .parameters = s.position(), .typeParameters = type.parameters });
        skipParameterList(s);
        skipBody(s);
        return fn;
    }
    case TokenKind::Equal: {
        s.advance();
        Word function = readNameInType(s, type);
        type.aliases.push_back({ .kind = kind, .index = index, .function = function });
        return std::nullopt;
    }
    case TokenKind::Colon:
        // TODO: A body that is just an expression, the value the function stores to its result
        s.error("Definitions by an expression are not supported yet");
    default:
        s.error("Expected '(' or '=' after definition name");
    }
}

void FileScanner::addType(TokenStream& s, TypeScan& type) {
    auto functionOf = [](const Definition& definition) { return definition.fn; };
    auto memberFns = type.members | std::views::transform(functionOf) | std::ranges::to<std::vector>();
    auto invariantFns = type.invariants | std::views::transform(functionOf) | std::ranges::to<std::vector>();

    ir::Database& db = out.database;
    ir::TypeImpl impl = db.addTypeImpl(memberFns, invariantFns, type.usageMap);
    VERIFY(impl.id() == out.typeImplNames.size());
    defineGlobal(s, type.name, GlobalName(impl));
    out.typeImplNames.emplace_back(context.wordTable.view(type.name));

    ir::InvariantList invariants = db.invariants(impl);
    for (int_t i = 0; i < (int_t)type.invariants.size(); i++) {
        const Definition& definition = type.invariants[i];
        GlobalName global { invariants.at(i) };
        VERIFY(global.id() == out.invariantNames.size());
        defineGlobal(s, definition.name, global);
        out.invariantNames.emplace_back(context.wordTable.view(definition.name));
        if (definition.fn.has_value())
            out.functions[definition.fn->id()].inlineOwner = global;
    }

    ir::MemberLiteralList members = db.members(impl);
    for (int_t i = 0; i < (int_t)type.members.size(); i++) {
        const Definition& definition = type.members[i];
        GlobalName global { members.at(i) };
        VERIFY(global.id() == out.memberNames.size());
        defineGlobal(s, definition.name, global);
        out.memberNames.emplace_back(context.wordTable.view(definition.name));
        if (definition.fn.has_value())
            out.functions[definition.fn->id()].inlineOwner = global;
    }

    for (const PendingAlias& alias : type.aliases) {
        GlobalName target = alias.kind == GlobalKind::Member
            ? GlobalName(members.at(alias.index))
            : GlobalName(invariants.at(alias.index));
        scanned.aliases.push_back({ .target = target, .function = alias.function });
    }
}

Word FileScanner::readTopLevelName(TokenStream& s) {
    if (s.tokKind() != TokenKind::GlobalName)
        s.error("Expected global name");
    Word name = s.tok().word();
    if (context.wordTable.view(name).starts_with(':'))
        s.error("A name beginning with ':' is only valid inside of a type");
    s.advance();
    return name;
}

Word FileScanner::readNameInType(TokenStream& s, const TypeScan& type) {
    if (s.tokKind() != TokenKind::GlobalName)
        s.error("Expected global name");
    Word name = s.tok().word();
    s.advance();
    std::string_view view = context.wordTable.view(name);
    if (!view.starts_with(':'))
        return name;
    std::string fullName = std::format("{}{}", context.wordTable.view(type.name), view);
    return context.wordTable.get(std::string_view(fullName));
}

ir::FnHandle FileScanner::newFunction(Word name) {
    ir::FnHandle fn = out.database.newFn();
    VERIFY(fn.id() == out.functions.size());
    FunctionInfo info;
    info.name = context.wordTable.view(name);
    out.functions.push_back(std::move(info));
    return fn;
}

void FileScanner::defineGlobal(TokenStream& s, Word name, GlobalName global) {
    if (context.globals.get(name).has_value())
        s.error(std::format("Global #{} is defined twice", context.wordTable.view(name)));
    context.globals.insert(name, global);
}

void FileScanner::skipParameterList(TokenStream& s) {
    VERIFY(s.tokKind() == TokenKind::LeftParen);
    s.advance();
    while (s.tokKind() != TokenKind::RightParen) {
        if (s.tokKind() == TokenKind::ContinueScope || s.tokKind() == TokenKind::EndScope)
            s.error("Expected ')' after parameters");
        s.advance();
    }
    s.advance();
}

void FileScanner::skipBody(TokenStream& s) {
    if (s.tokKind() != TokenKind::Colon)
        s.error("Expected ':' after parameters");
    s.advanceWithNoScopeChanges();
    if (s.tokKind() != TokenKind::BeginScope)
        s.error("Expected function body");
    s.skipScope();
}

ScannedFile ParseContext::scanFile(std::span<const Token> tokens) {
    VERIFY(!tokens.empty());
    VERIFY(tokens.front().kind() == TokenKind::BeginScope);
    VERIFY(tokens.back().kind() == TokenKind::EndScope);

    ScannedFile result;
    FileScanner scanner { *this, result };
    // The definitions are read from the root stream itself, a stream for the scope of the file
    // would move its parent past the end of the tokens once it is done
    auto s = TokenStream::makeRoot(tokens.data());
    s.advanceWithNoScopeChanges();
    scanner.scan(s);
    VERIFY(s.token == tokens.data() + tokens.size() - 1);
    return result;
}

void ParseContext::parseScannedFile(const ScannedFile& file) {
    for (const ScannedFile::Function& scanned : file.functions) {
        ParsedFunction parsed = parseScannedFunction(scanned);
        FunctionInfo& info = database.functions[scanned.fn.id()];
        info.parameterNames = std::move(parsed.parameterNames);
        info.labels = std::move(parsed.labels);
        info.theoremNames = std::move(parsed.theoremNames);
        database.database.setFn(scanned.fn, std::move(parsed.function));
    }

    for (const ScannedFile::Alias& alias : file.aliases) {
        auto global = globals.get(alias.function);
        if (!global.has_value())
            throw ParserException(std::format("Unknown function #{}", wordTable.view(alias.function)));
        if (global->kind() != GlobalKind::Function)
            throw ParserException(std::format("#{} is not a function", wordTable.view(alias.function)));
        if (alias.target.kind() == GlobalKind::Member)
            database.database.setTypeFn(alias.target.member(), global->fn());
        else
            database.database.setInvariantFn(alias.target.invariant(), global->fn());
    }
}

ParsedDatabase parseFile(const char* source) {
    ParseContext context;
    std::vector<Token> tokens = lexFile(source, context.wordTable);
    ScannedFile scanned = context.scanFile(tokens);
    context.parseScannedFile(scanned);
    return std::move(context.database);
}

}
