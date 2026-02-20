#pragma once

#include <WordTable.h>
#include <sema/Constant.h>
#include <parse/parse_gen.h>
#include <types.h>

#include <array>

namespace parse {

// ---------------------------- TokenKind ---------------------------

enum class TokenKind : uint8_t {
#define TOKEN(kind, lexToken, data1, data2) kind,
#include <parse/tokens.inc>

    COUNT,
    FirstUnaryExpr = LogicalNotExpr,
    LastUnaryExpr = DereferenceExpr,
};
std::string_view nameString(TokenKind);
LexerToken lexerToken(TokenKind);
inline bool isUnaryExpr(TokenKind kind) {
    return kind >= TokenKind::FirstUnaryExpr && kind <= TokenKind::LastUnaryExpr;
}

// ---------------------------- ScopeKind ---------------------------

#define ENUMERATE_SCOPE_KINDS     \
    SCOPE(Invalid)                \
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
    SCOPE(PlainStatement)         \
    SCOPE(Argument)               \
    SCOPE(Parameter)              \
    SCOPE(Namespace)              \
    SCOPE(FunctionBody)           \
    SCOPE(ReturnType)             \
    SCOPE(FunctionParameters)     \
    SCOPE(Struct)                 \
    SCOPE(Enum)                   \
    SCOPE(HasTypeExpr)            \
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

// ---------------------------- TokenInfo ----------------------------

struct TokenInfo : TaggedSourceLocation<TokenKind> {
    uint32_t data1Bits = 0;
    uint32_t data2Bits = 0;

    TokenInfo(TokenKind kind, SourceLocation location, uint32_t data1)
        : TaggedSourceLocation<TokenKind>(kind, location)
        , data1Bits(data1) { }

    TokenKind kind() const { return tag(); }

    template<typename T1>
    T1 data1() const {
        VERIFY(tokenDataTable[(size_t)kind()].data1Kind == DataTypeTrait<T1>::kind);
        return unpackData<T1>(data1Bits);
    }

    template<typename T2>
    T2 data2() const {
        VERIFY(tokenDataTable[(size_t)kind()].data2Kind == DataTypeTrait<T2>::kind);
        return unpackData<T2>(data2Bits);
    }

    template<typename T1>
    void setData1(T1 data1) {
        VERIFY(tokenDataTable[(size_t)kind()].data1Kind == DataTypeTrait<T1>::kind);
        data1Bits = packData<T1>(data1);
    }

    template<typename T2>
    void setData2(T2 data2) {
        VERIFY(tokenDataTable[(size_t)kind()].data2Kind == DataTypeTrait<T2>::kind);
        data2Bits = packData<T2>(data2);
    }

    void setKind(TokenKind kind) {
        setTag(kind);
    }
};

}