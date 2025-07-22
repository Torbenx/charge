#pragma once

#include <WordTranslationTable.h>
#include <sema/IdentifierTable.h>
#include <parse/Output.h>
#include <sema/Program.h>
#include <sema/Scope.h>


namespace sema {

struct ErrorBase;
struct Generator;

struct ErrorHandler {
    virtual void handleError(Generator&, ErrorBase&) = 0;
    virtual ~ErrorHandler() = default;
};

inline constexpr size_t MODULE_PROGRAM_ID_ALIGNMENT = 256;

struct ModuleImport {
    struct ModuleReference {
        ModuleHandle module;
        uint32_t programIdBegin;
        uint32_t programIdEnd;
    };

    const IdentifierTable* wordTable;
    std::vector<ModuleReference> modules;
    std::span<ProgramUnion> ownPrograms;
    std::span<const ModuleHandle> programModules; // TODO: This could be easily computed from the program id ranges
    std::span<Namespace> namespaces;

    ModuleReference selfReference() const { return modules.back(); }
};

struct Context {
    struct ModuleState {
        ProgramUnion* programStorage = nullptr; // Actual storage begins at programStorage + programIdBegin
        uint32_t programIdBegin = 0;
        uint32_t programIdEnd = 0;
        std::vector<uint16_t> programIdOffsets;
        std::vector<NamespaceHandle> namespaces;
    };

    parse::Output parseOutput;
    IdentifierTable wordTable { parse::words };
    std::vector<ModuleHandle> programModules;
    PageBumpAllocator<ProgramUnion> programStorage;
    PageBumpAllocator<Namespace> namespaces;
    std::vector<ModuleState> modules;
    struct ScopeStackEntry {
        DeclarationValue value;
        std::optional<Scope*> scope;
    };
    std::vector<ScopeStackEntry> m_scopeStack;
    std::vector<ProgramHandle> m_implDeclarations;
    ErrorHandler* errorHandler = nullptr;

    Context(std::span<const ModuleImport> imports, std::string_view source)
        : parseOutput(source) {
        initialize(imports);
    }
    void initialize(std::span<const ModuleImport>);
    ModuleImport exportModule();

    std::optional<Scope*> currentScope() { return m_scopeStack.back().scope; }
    Program* currentProgram() { return program(m_scopeStack.back().value.program()); }
    void popScope() { m_scopeStack.pop_back(); }
    void pushScope(DeclarationValue value, Scope* scope) { m_scopeStack.push_back({ value, scope }); }
    void pushEmptyScope(DeclarationValue value) { m_scopeStack.push_back({ value, std::nullopt }); }

    DeclarationValue pushStaticScope(ProgramKind kind, Word name, parse::TokenHandle parseLocation, SourceLocation location);
    DeclarationValue pushStaticImplScope(ProgramKind kind, parse::TokenHandle parseLocation, SourceLocation location);
    DeclarationValue pushNamespaceScope(Word name);
    DeclarationValue pushMemberScope(bool isHas, Word name, parse::TokenHandle parseLocation, SourceLocation location);
    DeclarationValue pushEnumValueScope(Word name, parse::TokenHandle parseLocation, SourceLocation location);

    ModuleHandle thisModule() const { return { static_cast<uint16_t>(modules.size() - 1) }; }
    bool isBuiltinModule() const { return modules.size() == 1; }

    ProgramHandle newProgram(ProgramKind kind, Word name, parse::TokenHandle parseLocation, DeclarationValue rawParent, SourceLocation location);
    Program* program(ProgramHandle handle) {
        auto& state = modules[moduleOf(handle).id()];
        VERIFY(handle.id() >= state.programIdBegin && handle.id() <= state.programIdEnd);
        return &state.programStorage[handle.id()].get();
    }
    ModuleHandle moduleOf(ProgramHandle handle) {
        return programModules[handle.id() / MODULE_PROGRAM_ID_ALIGNMENT];
    }
    ProgramHandle translate(ModuleHandle module, ProgramHandle program) {
        return { program.id() + modules[module.id()].programIdOffsets[program.id() / MODULE_PROGRAM_ID_ALIGNMENT] * (uint32_t)MODULE_PROGRAM_ID_ALIGNMENT };
    }
    ProgramHandle translate(ProgramHandle base, ProgramHandle program) {
        return translate(moduleOf(base), program);
    }
    ProgramHandle programHandle(Program* prog) {
        auto& state = modules.back();
        int_t id = reinterpret_cast<ProgramUnion*>(prog) - state.programStorage;
        VERIFY(id >= (int_t)state.programIdBegin && id < (int_t)state.programIdEnd);
        return ProgramHandle(id);
    }

    NamespaceHandle newNamespace(Word name, std::optional<NamespaceHandle> parent);
    Namespace* getNamespace(NamespaceHandle nsHandle) {
        return &namespaces[nsHandle.id()];
    }
    NamespaceHandle translate(ModuleHandle module, NamespaceHandle nsHandle) {
        return modules[module.id()].namespaces[nsHandle.id()];
    }
    NamespaceHandle translate(ProgramHandle base, NamespaceHandle nsHandle) {
        return translate(moduleOf(base), nsHandle);
    }
    NamespaceHandle namespaceHandle(Namespace* ns) {
        int_t id = ns - namespaces.data();
        return NamespaceHandle(id);
    }
    NamespaceHandle globalNamespace() { return NamespaceHandle(0); }

    DeclarationValue translate(ModuleHandle module, DeclarationValue value) {
        if (value.kind() == DeclarationValueKind::Program)
            return translate(module, value.program());
        if (value.kind() == DeclarationValueKind::Namespace)
            return translate(module, value.nsHandle());
        return value;
    }
    DeclarationValue translate(ProgramHandle base, DeclarationValue value) {
        return translate(moduleOf(base), value);
    }

    std::optional<Program*> firstDeclarationAfter(SourceLocation location);
    std::optional<Program*> lastDeclarationBefore(SourceLocation location);

    void checkBuiltins();
};

}