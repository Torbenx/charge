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
        Value value;
        std::optional<Scope*> scope;
    };
    std::vector<ScopeStackEntry> m_scopeStack;

    Context(std::string_view source)
        : parseOutput(source) {
        reset();
    }

    void reset() {
        programs.clear();
        namespaces.clear();
        identityProgramTranslation.clear();
        identityNamespaceTranslation.clear();
        m_scopeStack.clear();
        auto globalNamespace = newNamespace(Word(), std::nullopt);
        pushScope((Value)globalNamespace, getNamespace(globalNamespace));
    }

    std::optional<Scope*> currentScope() { return m_scopeStack.back().scope; }
    void popScope() { m_scopeStack.pop_back(); }
    void pushScope(Value value, Scope* scope) { m_scopeStack.push_back({ value, scope }); }
    void pushEmptyScope(Value value) { m_scopeStack.push_back({ value, std::nullopt }); }

    Value pushStaticScope(ProgramKind kind, Word name, parse::TokenHandle parseLocation, SourceLocation location) {
        ProgramHandle progHandle = newProgram(kind, name, parseLocation, m_scopeStack.back().value, location);

        std::optional<Scope*> scope = currentScope();
        if (scope.has_value())
            scope->addDeclaration(name, (Value)progHandle);

        if (kind == ProgramKind::Type) {
            pushScope((Value)progHandle, static_cast<TypeProgram*>(program(progHandle)));
        } else {
            pushEmptyScope((Value)progHandle);
        }
        return (Value)progHandle;
    }
    Value pushNamespaceScope(Word name) {
        std::optional<Scope*> scope = currentScope();
        VERIFY(scope.has_value());
        std::optional<Value> maybeResult = scope->getDeclaration(name);
        if (maybeResult.has_value()) {
            VERIFY(maybeResult->kind() == ValueKind::Namespace);
            return maybeResult.value();
        }
        auto nsHandle = newNamespace(name, NamespaceHandle());
        scope->addDeclaration(name, (Value)nsHandle);
        pushScope((Value)nsHandle, getNamespace(nsHandle));
        return (Value)nsHandle;
    }
    Value pushMemberScope(Word name, parse::TokenHandle parseLocation, SourceLocation location) {
        std::optional<Scope*> scope = currentScope();
        VERIFY(scope.has_value());
        TypeProgram* program = static_cast<TypeProgram*>(scope.value());
        VERIFY(program->kind() == ProgramKind::Type);
        int_t id = program->runtimeParameters.size();
        program->runtimeParameters.emplace_back(name, parseLocation, location);
        pushEmptyScope(INVALID_VALUE);
        return Value(ValueKind::Invalid, id);
    }

    ProgramHandle newProgram(ProgramKind kind, Word name, parse::TokenHandle parseLocation, Value rawParent, SourceLocation location) {
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
};

}