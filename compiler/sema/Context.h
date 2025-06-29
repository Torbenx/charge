#pragma once

#include <parse/Output.h>
#include <sema/Program.h>
#include <sema/Scope.h>

namespace sema {

struct Context {
    struct ModuleState {
        uint32_t programOffset;
        std::vector<NamespaceHandle> namespaces;
    };

    parse::Output parseOutput;
    WordStringTable wordTable { parse::words };
    PageBumpAllocator<Program*> programPointers;
    PageBumpAllocator<ModuleHandle> programModules;
    PageBumpAllocator<ProgramUnion> programStorage;
    PageBumpAllocator<Namespace> namespaces;
    std::vector<ModuleState> modules;
    struct ScopeStackEntry {
        DeclarationValue value;
        std::optional<Scope*> scope;
    };
    std::vector<ScopeStackEntry> m_scopeStack;
    std::vector<ProgramHandle> m_implDeclarations;
    ModuleHandle module;

    Context(std::string_view source)
        : parseOutput(source) {
        reset();
    }

    void reset() {
        parseOutput.reset();
        programPointers.clear();
        programModules.clear();
        namespaces.clear();
        programStorage.clear();
        m_scopeStack.clear();

        module = { (uint32_t)modules.size() };
        modules.emplace_back();
        modules.back().programOffset = programPointers.size();
        auto globalNamespace = newNamespace(Word(), std::nullopt);
        pushScope((DeclarationValue)globalNamespace, getNamespace(globalNamespace));
    }

    std::optional<Scope*> currentScope() { return m_scopeStack.back().scope; }
    Program* currentProgram() { return program(m_scopeStack.back().value.program()); }
    void popScope() { m_scopeStack.pop_back(); }
    void pushScope(DeclarationValue value, Scope* scope) { m_scopeStack.push_back({ value, scope }); }
    void pushEmptyScope(DeclarationValue value) { m_scopeStack.push_back({ value, std::nullopt }); }

    DeclarationValue pushStaticScope(ProgramKind kind, Word name, parse::TokenHandle parseLocation, SourceLocation location) {
        ProgramHandle progHandle = newProgram(kind, name, parseLocation, m_scopeStack.back().value, location);

        std::optional<Scope*> scope = currentScope();
        if (scope.has_value())
            scope->addDeclaration(name, (DeclarationValue)progHandle);

        auto scopeProg = try_cast<ScopeProgram>(program(progHandle));
        if (scopeProg.has_value()) {
            pushScope((DeclarationValue)progHandle, scopeProg.value());
        } else {
            pushEmptyScope((DeclarationValue)progHandle);
        }
        return (DeclarationValue)progHandle;
    }
    DeclarationValue pushStaticImplScope(ProgramKind kind, parse::TokenHandle parseLocation, SourceLocation location) {
        ProgramHandle progHandle = newProgram(kind, Word(), parseLocation, m_scopeStack.back().value, location);
        m_implDeclarations.push_back(progHandle);

        auto scopeProg = try_cast<ScopeProgram>(program(progHandle));
        if (scopeProg.has_value()) {
            pushScope((DeclarationValue)progHandle, scopeProg.value());
        } else {
            pushEmptyScope((DeclarationValue)progHandle);
        }
        return (DeclarationValue)progHandle;
    }
    DeclarationValue pushNamespaceScope(Word name) {
        std::optional<Scope*> scope = currentScope();
        VERIFY(scope.has_value());
        std::optional<DeclarationValue> maybeResult = scope->getDeclaration(name);
        if (maybeResult.has_value()) {
            VERIFY(maybeResult->kind() == DeclarationValueKind::Namespace);
            return maybeResult.value();
        }
        auto nsHandle = newNamespace(name, m_scopeStack.back().value.nsHandle());
        scope->addDeclaration(name, (DeclarationValue)nsHandle);
        pushScope((DeclarationValue)nsHandle, getNamespace(nsHandle));
        return (DeclarationValue)nsHandle;
    }
    DeclarationValue pushMemberScope(bool isHas, Word name, parse::TokenHandle parseLocation, SourceLocation location) {
        std::optional<Scope*> scope = currentScope();
        VERIFY(scope.has_value());
        StructProgram* program = static_cast<StructProgram*>(scope.value());
        VERIFY(program->kind() == ProgramKind::Struct);
        int_t id = program->members.size();
        program->members.emplace_back(location, isHas, name, parseLocation);
        pushEmptyScope(INVALID_DECLARATION_VALUE); // TODO: Avoid this
        return DeclarationValue(DeclarationValueKind::Member, id);
    }
    DeclarationValue pushEnumValueScope(Word name, parse::TokenHandle parseLocation, SourceLocation location) {
        std::optional<Scope*> scope = currentScope();
        VERIFY(scope.has_value());
        EnumProgram* program = static_cast<EnumProgram*>(scope.value());
        VERIFY(program->kind() == ProgramKind::Enum);
        int_t id = program->values.size();
        program->values.emplace_back(location, name, parseLocation);
        program->addDeclaration(name, DeclarationValue(DeclarationValueKind::EnumValue, id));
        pushEmptyScope(INVALID_DECLARATION_VALUE);
        return DeclarationValue(DeclarationValueKind::EnumValue, id);
    }

    ProgramHandle newProgram(ProgramKind kind, Word name, parse::TokenHandle parseLocation, DeclarationValue rawParent, SourceLocation location) {
        ProgramHandle result = { (uint32_t)programPointers.size() };
        auto* prog = programStorage.allocate();
        programPointers.push_back(&prog->get());
        programModules.push_back(module);
        std::construct_at(prog, kind, name, parseLocation, rawParent, location);
        return result;
    }
    Program* program(ProgramHandle handle) {
        return programPointers[handle.id()];
    }
    ModuleHandle moduleOf(ProgramHandle handle) {
        return programModules[handle.id()];
    }
    ProgramHandle translate(ModuleHandle module, ProgramHandle program) {
        return { modules[module.id()].programOffset + program.id() };
    }
    ProgramHandle translate(ProgramHandle base, ProgramHandle program) {
        return translate(moduleOf(base), program);
    }
    ProgramHandle programHandle(Program* prog) {
        int_t id = reinterpret_cast<ProgramUnion*>(prog) - programStorage.data();
        VERIFY(id >= 0 && id < programStorage.size());
        return ProgramHandle(id);
    }

    NamespaceHandle newNamespace(Word name, std::optional<NamespaceHandle> parent) {
        VERIFY((int_t)modules[module.id()].namespaces.size() == namespaces.size());
        NamespaceHandle result = { (uint32_t)namespaces.size() };
        modules[module.id()].namespaces.push_back(result);
        auto* ns = namespaces.allocate();
        std::construct_at(ns, name, parent);
        return result;
    }
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

    std::optional<Program*> firstDeclarationAfter(SourceLocation location) {
        auto compare = [](ProgramUnion& prog, SourceLocation location) {
            return prog.get().declarationLocation() < location;
        };
        auto it = std::lower_bound(programStorage.begin(), programStorage.end(), location, compare);
        if (it == programStorage.end())
            return std::nullopt;
        return &it->get();
    }
    std::optional<Program*> lastDeclarationBefore(SourceLocation location) {
        auto compare = [](ProgramUnion& prog, SourceLocation location) {
            return prog.get().declarationLocation() < location;
        };
        auto it = std::lower_bound(programStorage.begin(), programStorage.end(), location, compare);
        if (it == programStorage.begin())
            return std::nullopt;
        return &std::prev(it)->get();
    }
};

}