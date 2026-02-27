#pragma once

#include <sema/Context.h>
#include <sema/Util.h>

namespace server {

enum class LocalDeclarationKind : uint8_t {
    TemplateParameter,
    FunctionParameter,
    Variable,
    Reference,
};
struct LocalDeclaration {
    LocalDeclarationKind kind;
    uint32_t id;
    sema::ProgramHandle declaringProgram;

    bool operator==(const LocalDeclaration&) const = default;
};
struct MemberDeclaration {
    sema::ProgramHandle structProgram;
    uint32_t memberIndex;

    bool operator==(const MemberDeclaration&) const = default;
};
struct EnumValueDeclaration {
    sema::ProgramHandle enumProgram;
    uint32_t valueIndex;

    bool operator==(const EnumValueDeclaration&) const = default;
};
using DeclarationInfo = std::variant<std::monostate, sema::NamespaceHandle, sema::ProgramHandle, MemberDeclaration, EnumValueDeclaration, LocalDeclaration>;

struct SemaUtil : sema::Util {
    using Util::Util;

    DeclarationInfo extractDeclarationInfo(const parse::TokenInfo&);
    std::optional<SourceLocation> declarationLocation(const DeclarationInfo&);
};

struct SemaContext : sema::Context {
    using Context::Context;

    void signatureCheckAll();
    void makeScratchProgram();

    sema::ProgramHandle scratchProgram() const {
        VERIFY(m_scratchProgram.has_value());
        return m_scratchProgram.value();
    }

    SemaUtil utilFor(parse::TokenHandle tokHandle) {
        return { *this, containingProgram(tokHandle).value_or(scratchProgram()) };
    }

    std::optional<parse::TokenHandle> containingIdentifier(SourceLocation);

    template<typename F>
    void forEachToken(const F& f) {
        forEachTokenImpl(&f, [](const void* fPtr, SemaUtil& util, parse::TokenHandle tokHandle) {
            (*static_cast<const F*>(fPtr))(util, tokHandle);
        });
    }

private:
    std::optional<sema::ProgramHandle> m_scratchProgram;

    void forEachTokenImpl(const void* data, void (*)(const void*, SemaUtil&, parse::TokenHandle));
};

}