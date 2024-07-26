#pragma once

#include <parse/Output.h>
#include <sema/Program.h>
#include <sema/Scope.h>

namespace sema {

struct Context {
    parse::Output parseOutput;
    WordStringTable wordTable { parse::words };
    PageBumpAllocator<ProgramUnion> programs;
    PageBumpAllocator<Namespace> namespaces;
    PageBumpAllocator<ProgramHandle> identityProgramTranslation;
    PageBumpAllocator<NamespaceHandle> identityNamespaceTranslation;
    struct ScopeStackEntry {
        ScopeValue value;
        std::optional<Scope*> scope;
    };
    std::vector<ScopeStackEntry> m_scopeStack;

    Context(std::string_view source)
        : parseOutput(source) {
        reset();
    }

    void reset() {
        parseOutput.reset();
        programs.clear();
        namespaces.clear();
        identityProgramTranslation.clear();
        identityNamespaceTranslation.clear();
        m_scopeStack.clear();
        auto globalNamespace = newNamespace(Word(), std::nullopt);
        pushScope((ScopeValue)globalNamespace, getNamespace(globalNamespace));
    }

    std::optional<Scope*> currentScope() { return m_scopeStack.back().scope; }
    void popScope() { m_scopeStack.pop_back(); }
    void pushScope(ScopeValue value, Scope* scope) { m_scopeStack.push_back({ value, scope }); }
    void pushEmptyScope(ScopeValue value) { m_scopeStack.push_back({ value, std::nullopt }); }

    ScopeValue pushStaticScope(ProgramKind kind, Word name, parse::TokenHandle parseLocation, SourceLocation location) {
        ProgramHandle progHandle = newProgram(kind, name, parseLocation, m_scopeStack.back().value, location);

        std::optional<Scope*> scope = currentScope();
        if (scope.has_value())
            scope->addDeclaration(name, (ScopeValue)progHandle);

        if (kind == ProgramKind::Type) {
            pushScope((ScopeValue)progHandle, cast<TypeProgram>(program(progHandle)));
        } else {
            pushEmptyScope((ScopeValue)progHandle);
        }
        return (ScopeValue)progHandle;
    }
    ScopeValue pushNamespaceScope(Word name) {
        std::optional<Scope*> scope = currentScope();
        VERIFY(scope.has_value());
        std::optional<ScopeValue> maybeResult = scope->getDeclaration(name);
        if (maybeResult.has_value()) {
            VERIFY(maybeResult->kind() == ValueKind::Namespace);
            return maybeResult.value();
        }
        auto nsHandle = newNamespace(name, m_scopeStack.back().value.nsHandle());
        scope->addDeclaration(name, (ScopeValue)nsHandle);
        pushScope((ScopeValue)nsHandle, getNamespace(nsHandle));
        return (ScopeValue)nsHandle;
    }
    ScopeValue pushMemberScope(Word name, parse::TokenHandle parseLocation, SourceLocation location) {
        std::optional<Scope*> scope = currentScope();
        VERIFY(scope.has_value());
        TypeProgram* program = static_cast<TypeProgram*>(scope.value());
        VERIFY(program->kind() == ProgramKind::Type);
        int_t id = program->runtimeParameters.size();
        program->runtimeParameters.emplace_back(name, parseLocation, location);
        pushEmptyScope(ScopeValue());
        return ScopeValue(Value(ValueKind::Invalid, id));
    }

    ProgramHandle newProgram(ProgramKind kind, Word name, parse::TokenHandle parseLocation, ScopeValue rawParent, SourceLocation location) {
        ProgramHandle result = { (uint32_t)programs.size() };
        identityProgramTranslation.push_back(result);
        auto* prog = programs.allocate();
        std::construct_at(prog, kind, name, parseLocation, rawParent, location);
        prog->get().programTranslationBuffer = identityProgramTranslation.data();
        prog->get().namespaceTranslationBuffer = identityNamespaceTranslation.data();
        return result;
    }
    Program* program(ProgramHandle handle) {
        return &programs[handle.id()].get();
    }
    ProgramHandle programHandle(Program* prog) {
        int_t id = reinterpret_cast<ProgramUnion*>(prog) - programs.data();
        return ProgramHandle(id);
    }

    NamespaceHandle newNamespace(Word name, std::optional<NamespaceHandle> parent) {
        NamespaceHandle result = { (uint32_t)namespaces.size() };
        identityNamespaceTranslation.push_back(result);
        auto* ns = namespaces.allocate();
        std::construct_at(ns, name, parent);
        return result;
    }
    Namespace* getNamespace(NamespaceHandle nsHandle) {
        return &namespaces[nsHandle.id()];
    }
    NamespaceHandle namespaceHandle(Namespace* ns) {
        int_t id = ns - namespaces.data();
        return NamespaceHandle(id);
    }

    std::optional<Program*> firstDeclarationAfter(SourceLocation location) {
        auto compare = [](ProgramUnion& prog, SourceLocation location) {
            return prog.get().declarationLocation() < location;
        };
        auto it = std::lower_bound(programs.begin(), programs.end(), location, compare);
        if (it == programs.end())
            return std::nullopt;
        return &it->get();
    }
    std::optional<Program*> lastDeclarationBefore(SourceLocation location) {
        auto compare = [](ProgramUnion& prog, SourceLocation location) {
            return prog.get().declarationLocation() < location;
        };
        auto it = std::lower_bound(programs.begin(), programs.end(), location, compare);
        if (it == programs.begin())
            return std::nullopt;
        return &std::prev(it)->get();
    }
};

}