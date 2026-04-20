#pragma once

#include <WordTable.h>
#include <sema/Constant.h>
#include <types.h>

#include <array>

namespace parse {

// ---------------------------- TokenKind ---------------------------

enum class LexerToken : uint8_t;

enum class TokenKind : uint8_t {
#define TOKEN(kind, lexToken, data1, data2) kind,
#define MARKER(name) name, _##name##reset = name - 1,
#include <parse/tokens.inc>

    COUNT,
};
std::string_view nameString(TokenKind);
LexerToken lexerToken(TokenKind);
inline bool isExpression(TokenKind kind) {
    return kind >= TokenKind::ExpressionsBegin && kind < TokenKind::ExpressionsEnd;
}
inline bool isUnaryExpr(TokenKind kind) {
    return kind >= TokenKind::UnaryExprsBegin && kind < TokenKind::UnaryExprsEnd;
}
inline bool isProgramDecl(TokenKind kind) {
    return kind >= TokenKind::ProgramDeclsBegin && kind < TokenKind::ProgramDeclsEnd;
}
inline bool isEnumValueDecl(TokenKind kind) {
    return kind >= TokenKind::EnumValueDeclsBegin && kind < TokenKind::EnumValueDeclsEnd;
}
inline bool isMemberDecl(TokenKind kind) {
    return kind >= TokenKind::MemberDeclsBegin && kind < TokenKind::MemberDeclsEnd;
}
inline bool isVariableDecl(TokenKind kind) {
    return kind >= TokenKind::VariableDeclsBegin && kind < TokenKind::VariableDeclsEnd;
}

// ---------------------------- ScopeKind ---------------------------

#define ENUMERATE_SCOPE_KINDS     \
    SCOPE(IfExpr)                 \
    SCOPE(IfExprOrStmt)           \
    SCOPE(CompoundStmt)           \
    SCOPE(Paren)                  \
    SCOPE(ParenInImplExpr)        \
    SCOPE(Square)                 \
    SCOPE(Brace)                  \
    SCOPE(BraceInImplExpr)        \
    SCOPE(LeftExpr)               \
    SCOPE(RightExpr)              \
    SCOPE(VariableType)           \
    SCOPE(IfBranch)               \
    SCOPE(ElseBranch)             \
    SCOPE(Parameter)              \
    SCOPE(Namespace)              \
    SCOPE(FunctionBody)           \
    SCOPE(ReturnType)             \
    SCOPE(FunctionParameters)     \
    SCOPE(Struct)                 \
    SCOPE(Enum)                   \
    SCOPE(BaseTypeExpr)           \
    SCOPE(TemplateParameters)     \
    SCOPE(StructImplExpression)   \
    SCOPE(FunctionImplExpression) \
    SCOPE(EnumImplExpression)     \
    SCOPE(GlobalImplExpression)   \
    SCOPE(GenericCategoryExpression)

enum class ScopeKind : uint8_t {
#define SCOPE(kind) kind,
    ENUMERATE_SCOPE_KINDS
#undef SCOPE

    COUNT,
    Invalid = COUNT,
};
std::string_view nameString(ScopeKind);

// -------------------------- CallArguments -------------------------

struct CallArgumentsHandle {
    uint32_t offset;
};

// ---------------------------- DataKind ----------------------------

enum class DataKind : uint8_t {
#define DATAKIND(type, name) name,
#include <parse/tokens.inc>
};
std::string_view nameString(DataKind);
template<DataKind kind>
struct DataTrait;
#define DATAKIND(cppType, name)        \
    template<>                         \
    struct DataTrait<DataKind::name> { \
        using type = cppType;          \
    };
#include <parse/tokens.inc>
template<DataKind kind>
using data_t = typename DataTrait<kind>::type;

template<typename T>
struct DataTypeTrait;
#define DATAKIND(cppType, name)                          \
    template<>                                           \
    struct DataTypeTrait<cppType> {                      \
        static constexpr DataKind kind = DataKind::name; \
    };
#include <parse/tokens.inc>

struct DataTableEntry {
    DataKind data1Kind : 4;
    DataKind data2Kind : 4;
};
static_assert(sizeof(DataTableEntry) == 1);
#define TOKEN(kind, lexToken, data1, data2) DataTableEntry { DataKind::data1, DataKind::data2 },
inline constexpr std::array<DataTableEntry, (size_t)TokenKind::COUNT> tokenDataTable {
#include <parse/tokens.inc>
};

template<typename T>
constexpr uint32_t packData(T data) {
    if constexpr (sizeof(T) == 1)
        return std::bit_cast<uint8_t>(data);
    else if constexpr (sizeof(T) == 2)
        return std::bit_cast<uint16_t>(data);
    else
        return std::bit_cast<uint32_t>(data);
}

template<typename T>
constexpr T unpackData(uint32_t bits) {
    if constexpr (sizeof(T) == 1)
        return std::bit_cast<T>((uint8_t)bits);
    else if constexpr (sizeof(T) == 2)
        return std::bit_cast<T>((uint16_t)bits);
    else
        return std::bit_cast<T>(bits);
}

template<typename T1>
constexpr uint32_t packData1(TokenKind tokenKind, T1 data1) {
    VERIFY(tokenDataTable[(size_t)tokenKind].data1Kind == DataTypeTrait<T1>::kind);
    return packData<T1>(data1);
}

template<typename T2>
constexpr uint32_t packData2(TokenKind tokenKind, T2 data2) {
    VERIFY(tokenDataTable[(size_t)tokenKind].data2Kind == DataTypeTrait<T2>::kind);
    return packData<T2>(data2);
}

// ---------------------------- TokenInfo ----------------------------

struct TokenInfo : TaggedSourceLocation<TokenKind> {
    uint32_t data1Bits = 0;
    uint32_t data2Bits = 0;

    TokenInfo(TokenKind kind, SourceLocation location, uint32_t data1)
        : TaggedSourceLocation<TokenKind>(kind, location)
        , data1Bits(data1) { }

    TokenKind kind() const { return tag(); }

    bool hasData1(DataKind dKind) const {
        return tokenDataTable[(size_t)kind()].data1Kind == dKind;
    }

    bool hasData2(DataKind dKind) const {
        return tokenDataTable[(size_t)kind()].data2Kind == dKind;
    }

    template<DataKind dKind>
    auto data1() const {
        VERIFY(hasData1(dKind));
        return unpackData<data_t<dKind>>(data1Bits);
    }

    template<DataKind dKind>
    auto data2() const {
        VERIFY(hasData2(dKind));
        return unpackData<data_t<dKind>>(data2Bits);
    }

    template<DataKind dKind>
    void setData1(data_t<dKind> data1) {
        VERIFY(hasData1(dKind));
        data1Bits = packData(data1);
    }

    template<DataKind dKind>
    void setData2(data_t<dKind> data2) {
        VERIFY(hasData2(dKind));
        data2Bits = packData(data2);
    }

    void setKind(TokenKind kind) {
        setTag(kind);
    }
};

}