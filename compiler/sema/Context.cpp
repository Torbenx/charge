#include <sema/Context.h>

namespace sema {

void Context::initialize(std::span<const ModuleInput> inputs) {
    modules.resize(inputs.size() + 1);

    // Make the global namespace
    VERIFY(newNamespace(Word(), std::nullopt) == globalNamespace());
    pushScope((DeclarationValue)globalNamespace(), getNamespace(globalNamespace()));

    // Inputs are assumend to be topologically orderered
    for (int_t moduleId = 0; moduleId < (int_t)inputs.size(); moduleId++) {
        const auto& input = inputs[moduleId];
        auto& output = modules[moduleId];
        ModuleHandle moduleHandle = { (uint16_t)moduleId };

        // Words
        WordTranslationTable inputToOutputWords;
        WordTranslationTable outputToInputWords;
        input.wordTable->forEachWord([this, &inputToOutputWords, &outputToInputWords](Word inputWord, std::string_view string) {
            Word outputWord = wordTable.getWithHash(string, inputWord.hash());
            inputToOutputWords.insert(inputWord, outputWord);
            outputToInputWords.insert(outputWord, inputWord);
        });

        // Programs
        output.programIdBegin = programModules.size() * MODULE_PROGRAM_ID_ALIGNMENT;
        output.programIdEnd = output.programIdBegin + input.ownPrograms.size();
        output.programStorage = input.ownPrograms.data() - output.programIdBegin;

        output.programIdOffsets.resize(input.programModules.size());
        for (int_t i = 0; i < (int_t)output.programIdOffsets.size(); i++) {
            ModuleHandle externHandle = input.programModules[i];
            ModuleHandle localHandle = input.modules[externHandle.id()].module;
            programModules.push_back(localHandle);
            int_t beginDiff = (int_t)modules[localHandle.id()].programIdBegin - (int_t)input.modules[externHandle.id()].programIdBegin;
            VERIFY(beginDiff >= 0);
            output.programIdOffsets[i] = beginDiff / MODULE_PROGRAM_ID_ALIGNMENT;
        }

        // Namespaces
        for (const Namespace& inNamespace : input.namespaces) {
            if (!inNamespace.parent.has_value()) {
                output.namespaces.push_back(globalNamespace());
                continue;
            }

            Word outName = inputToOutputWords.get(inNamespace.name);
            NamespaceHandle outParent = output.namespaces.at(inNamespace.parent.value().id());
            auto existingDecl = getNamespace(outParent)->getDeclaration(outName);
            NamespaceHandle outHandle;
            if (existingDecl.has_value()) {
                VERIFY(existingDecl.value().kind() == DeclarationValueKind::Namespace);
                outHandle = existingDecl.value().nsHandle();
            } else {
                outHandle = newNamespace(outName, outParent);
                getNamespace(outParent)->addDeclaration(outName, outHandle);
            }
            output.namespaces.push_back(outHandle);
            Namespace& outNamespace = *getNamespace(outHandle);

            inNamespace.forEachDeclration([this, moduleHandle, &outNamespace, &inputToOutputWords](Word inDeclName, DeclarationValue inDecl) {
                if (inDecl.kind() != DeclarationValueKind::Program) {
                    VERIFY(inDecl.kind() == DeclarationValueKind::Namespace); // Only namespaces and programs expected at namespace level
                    return;
                }

                ProgramHandle outProg = translate(moduleHandle, inDecl.program());
                Word outDeclName = inputToOutputWords.get(inDeclName);
                auto outResult = outNamespace.getDeclaration(outDeclName);
                // Programs form this module should be new, all others should already exist
                if (outResult.has_value()) {
                    VERIFY(outResult.value().kind() == DeclarationValueKind::Program);
                    VERIFY(outResult.value().program() == outProg);
                    VERIFY(moduleOf(outProg) != moduleHandle);
                } else {
                    VERIFY(moduleOf(outProg) == moduleHandle);
                    outNamespace.addDeclaration(outDeclName, outProg);
                }
            });
        }
        VERIFY(output.namespaces.size() == input.namespaces.size());
    }

    // Setups this module
    VERIFY(programStorage.size() == 0);
    auto& thisOutput = modules.back();
    thisOutput.programIdBegin = programModules.size() * MODULE_PROGRAM_ID_ALIGNMENT;
    thisOutput.programIdEnd = thisOutput.programIdBegin;
    thisOutput.programStorage = programStorage.data() - thisOutput.programIdBegin;
    thisOutput.programIdOffsets.resize(thisOutput.programIdEnd, 0);
}

DeclarationValue Context::pushStaticScope(ProgramKind kind, Word name, parse::TokenHandle parseLocation, SourceLocation location) {
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

DeclarationValue Context::pushStaticImplScope(ProgramKind kind, parse::TokenHandle parseLocation, SourceLocation location) {
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

DeclarationValue Context::pushNamespaceScope(Word name) {
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

DeclarationValue Context::pushMemberScope(bool isHas, Word name, parse::TokenHandle parseLocation, SourceLocation location) {
    std::optional<Scope*> scope = currentScope();
    VERIFY(scope.has_value());
    StructProgram* program = static_cast<StructProgram*>(scope.value());
    VERIFY(program->kind() == ProgramKind::Struct);
    int_t id = program->members.size();
    program->members.emplace_back(location, isHas, name, parseLocation);
    pushEmptyScope(INVALID_DECLARATION_VALUE); // TODO: Avoid this
    return DeclarationValue(DeclarationValueKind::Member, id);
}

DeclarationValue Context::pushEnumValueScope(Word name, parse::TokenHandle parseLocation, SourceLocation location) {
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

ProgramHandle Context::newProgram(ProgramKind kind, Word name, parse::TokenHandle parseLocation, DeclarationValue rawParent, SourceLocation location) {
    auto& state = modules.back();
    VERIFY(programStorage.size() == state.programIdEnd - state.programIdBegin);
    ProgramHandle result = { state.programIdEnd };
    state.programIdEnd += 1;

    auto* prog = programStorage.allocate();
    std::construct_at(prog, kind, name, parseLocation, rawParent, location);

    VERIFY(programModules.size() == (int_t)state.programIdOffsets.size());
    if (result.id() / MODULE_PROGRAM_ID_ALIGNMENT >= (size_t)programModules.size()) {
        programModules.push_back(thisModule());
        state.programIdOffsets.push_back(0);
    }

    return result;
}

NamespaceHandle Context::newNamespace(Word name, std::optional<NamespaceHandle> parent) {
    VERIFY((int_t)modules.back().namespaces.size() == namespaces.size());
    NamespaceHandle result = { (uint32_t)namespaces.size() };
    modules.back().namespaces.push_back(result);
    auto* ns = namespaces.allocate();
    std::construct_at(ns, name, parent);
    return result;
}

std::optional<Program*> Context::firstDeclarationAfter(SourceLocation location) {
    auto compare = [](ProgramUnion& prog, SourceLocation location) {
        return prog.get().declarationLocation() < location;
    };
    auto it = std::lower_bound(programStorage.begin(), programStorage.end(), location, compare);
    if (it == programStorage.end())
        return std::nullopt;
    return &it->get();
}

std::optional<Program*> Context::lastDeclarationBefore(SourceLocation location) {
    auto compare = [](ProgramUnion& prog, SourceLocation location) {
        return prog.get().declarationLocation() < location;
    };
    auto it = std::lower_bound(programStorage.begin(), programStorage.end(), location, compare);
    if (it == programStorage.begin())
        return std::nullopt;
    return &std::prev(it)->get();
}

}