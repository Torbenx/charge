#include <sema/Context.h>

#include <parse/parse_gen.h>
#include <sema/Generator.h>

namespace sema {

#define BUILTIN(name, cppName) void check_##cppName(Context&);
#include <sema/builtins.inc>

void Context::initialize(std::span<const ModuleImport> imports) {
    modules.resize(imports.size() + 1);

    // Make the global namespace
    VERIFY(newNamespace(Word(), std::nullopt) == globalNamespace());
    pushScope((DeclarationValue)globalNamespace(), getNamespace(globalNamespace()));

    // Inputs are assumend to be topologically orderered
    for (int_t moduleId = 0; moduleId < (int_t)imports.size(); moduleId++) {
        const auto& input = imports[moduleId];
        auto& output = modules[moduleId];
        ModuleHandle moduleHandle = { (uint16_t)moduleId };

        // Words
        WordTranslationTable inputToOutputWords;
        WordTranslationTable outputToInputWords;
        input.wordTable->forEachWord([this, &inputToOutputWords, &outputToInputWords](Word inputWord, std::string_view string) {
            Word outputWord = tokenBuffer.wordTable.getWithHash(string, inputWord.hash());
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

        // Impls
        implTable.growTo(output.programIdEnd, {});
        for (uint32_t progIndex = 0; progIndex < input.ownPrograms.size(); progIndex++) {
            ProgramHandle progHandle { output.programIdBegin + progIndex };
            auto& prog = input.ownPrograms[progIndex].get();
            if (prog.isImpl()) {
                auto externImplProg = prog.baseProgram(prog.selfConstant());
                VERIFY(externImplProg.has_value());
                ProgramHandle localImplProg = translate(moduleHandle, externImplProg.value());
                implTable[localImplProg.id()].push_back(progHandle);
            }
        }

        // Namespaces
        for (const Namespace& inNamespace : input.namespaces) {
            NamespaceHandle outHandle;
            if (!inNamespace.parent.has_value()) {
                outHandle = globalNamespace();
            } else {
                Word outName = inputToOutputWords.get(inNamespace.name);
                NamespaceHandle outParent = output.namespaces.at(inNamespace.parent.value().id());
                auto existingDecl = getNamespace(outParent)->getDeclaration(outName);
                if (existingDecl.has_value()) {
                    VERIFY(existingDecl.value().kind() == DeclarationValueKind::Namespace);
                    outHandle = existingDecl.value().nsHandle();
                } else {
                    outHandle = newNamespace(outName, outParent);
                    getNamespace(outParent)->addDeclaration(outName, outHandle);
                }
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
                // Programs from this module should be new, all others should already exist
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
    thisOutput.programIdOffsets.resize(programModules.size(), 0);
    thisOutput.programIdBegin = programModules.size() * MODULE_PROGRAM_ID_ALIGNMENT;
    thisOutput.programIdEnd = thisOutput.programIdBegin;
    thisOutput.programStorage = programStorage.data() - thisOutput.programIdBegin;
    implTable.growTo(thisOutput.programIdBegin, {});

    // Reserve slots for builtin programs
    if (isBuiltinModule()) {
        static_assert((size_t)BuiltinId::COUNT <= MODULE_PROGRAM_ID_ALIGNMENT);
        VERIFY(programModules.size() == 0);
        VERIFY(thisOutput.programIdOffsets.size() == 0);
        VERIFY(thisOutput.programIdBegin == 0);
        VERIFY(thisOutput.programIdEnd == 0);
        programModules.push_back(thisModule());
        thisOutput.programIdOffsets.push_back(0);
        for (int_t builtinId = 0; builtinId < (int_t)BuiltinId::COUNT; builtinId++) {
            programStorage.allocate();
            implTable.push_back({});
        }
        thisOutput.programIdEnd = (uint32_t)BuiltinId::COUNT;
    }
}

ModuleImport Context::exportModule() {
    std::vector<ModuleImport::ModuleReference> references;
    references.resize(modules.size());
    for (int_t moduleId = 0; moduleId < (int_t)modules.size(); moduleId++) {
        references[moduleId] = {
            .module = ModuleHandle(moduleId),
            .programIdBegin = modules[moduleId].programIdBegin,
            .programIdEnd = modules[moduleId].programIdEnd,
        };
    }
    return {
        .wordTable = &tokenBuffer.wordTable,
        .modules = std::move(references),
        .ownPrograms = programStorage,
        .programModules = programModules,
        .namespaces = namespaces
    };
}

void Context::completeSignatureCheck(ProgramHandle progHandle, bool isImpl, Constant selfConstant) {
    auto* prog = program(progHandle);
    prog->markSignatureCheckComplete(isImpl, selfConstant);
    if (isImpl) {
        auto implOf = prog->baseProgram(selfConstant);
        VERIFY(implOf.has_value());
        implTable[implOf.value().id()].push_back(progHandle);
    }
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

DeclarationValue Context::pushMemberScope(bool isBase, Word name, parse::TokenHandle parseLocation, SourceLocation location) {
    std::optional<Scope*> scope = currentScope();
    VERIFY(scope.has_value());
    StructProgram* program = static_cast<StructProgram*>(scope.value());
    VERIFY(program->kind() == ProgramKind::Struct);
    int_t id = program->members.size();
    program->members.emplace_back(location, isBase, name, parseLocation);
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

namespace {

    std::optional<ProgramHandle> getBuiltin(Word name) {
#define BUILTIN(builtinName, builtinCppName) \
    if (name == parse::words[#builtinName])  \
        return ProgramHandle(BuiltinId::builtinCppName);
#include <sema/builtins.inc>

        return std::nullopt;
    }

}

ProgramHandle Context::newProgram(ProgramKind kind, Word name, parse::TokenHandle parseLocation, DeclarationValue rawParent, SourceLocation location) {
    auto& state = modules.back();
    VERIFY(programStorage.size() == state.programIdEnd - state.programIdBegin);
    VERIFY(implTable.size() == state.programIdEnd);

    if (isBuiltinModule() && rawParent == globalNamespace()) {
        auto builtinHandle = getBuiltin(name);
        if (builtinHandle.has_value()) {
            std::construct_at(&programStorage[builtinHandle.value().id()], kind, name, parseLocation, rawParent, location);
            return builtinHandle.value();
        }
    }
    ProgramHandle result = { state.programIdEnd };
    state.programIdEnd += 1;
    auto* prog = programStorage.allocate();
    std::construct_at(prog, kind, name, parseLocation, rawParent, location);
    implTable.push_back({});

    VERIFY(programModules.size() == state.programIdOffsets.size());
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

std::optional<ProgramHandle> Context::containingProgram(parse::TokenHandle tok) {
    auto it = std::partition_point(programStorage.begin(), programStorage.end(), [tok](ProgramUnion& prog) {
        return prog.get().tokenRangeBegin <= tok;
    });
    if (it == programStorage.begin())
        return std::nullopt;
    VERIFY(it == programStorage.end() || it->get().tokenRangeBegin > tok);
    VERIFY(std::prev(it)->get().tokenRangeBegin <= tok);
    Program* prog = &std::prev(it)->get();
    while (!prog->tokenRange().contains(tok)) {
        if (prog->parent().kind() != DeclarationValueKind::Program)
            return std::nullopt;
        prog = program(prog->parent().program());
    }
    return ownProgramHandle(prog);
}

std::optional<ProgramHandle> Context::firstDeclarationAfter(SourceLocation location) {
    auto compare = [](SourceLocation location, ProgramUnion& prog) {
        return location < prog.get().declarationLocation();
    };
    auto it = std::upper_bound(programStorage.begin(), programStorage.end(), location, compare);
    if (it == programStorage.end())
        return std::nullopt;
    return ownProgramHandle(&it->get());
}

std::optional<ProgramHandle> Context::lastDeclarationAtOrBefore(SourceLocation location) {
    auto compare = [](SourceLocation location, ProgramUnion& prog) {
        return location < prog.get().declarationLocation();
    };
    auto it = std::upper_bound(programStorage.begin(), programStorage.end(), location, compare);
    if (it == programStorage.begin())
        return std::nullopt;
    return ownProgramHandle(&std::prev(it)->get());
}

void Context::checkBuiltins() {
    {
        auto* prog = cast<StructProgram>(program(builtins::type_type.program()));
        VERIFY(!prog->isDependent());
        VERIFY(prog->members.empty());
    }

    {
        auto* prog = cast<EnumProgram>(program(builtins::bool_type.program()));
        VERIFY(!prog->isDependent());
        VERIFY(prog->values.size() == 2);
        VERIFY(prog->values[0].name() == parse::words["false"]);
        VERIFY(prog->values[1].name() == parse::words["true"]);
    }

    /*{
        auto* prog = cast<StructProgram>(program(builtins::namespace_type.program()));
        VERIFY(!prog->isDependent());
        VERIFY(prog->members.empty());
    }*/

    {
        auto* prog = cast<EnumProgram>(program(builtins::expression_category_type.program()));
        VERIFY(!prog->isDependent());
        VERIFY(prog->values.size() == 5);
        VERIFY(prog->values[std::to_underlying(ExpressionCategory::Value)].name() == parse::words["value"]);
        VERIFY(prog->values[std::to_underlying(ExpressionCategory::UniqueReference)].name() == parse::words["unique_ref"]);
        VERIFY(prog->values[std::to_underlying(ExpressionCategory::ConstUniqueReference)].name() == parse::words["const_unique_ref"]);
        VERIFY(prog->values[std::to_underlying(ExpressionCategory::SharedReference)].name() == parse::words["shared_ref"]);
        VERIFY(prog->values[std::to_underlying(ExpressionCategory::ConstSharedReference)].name() == parse::words["const_shared_ref"]);
    }

    // template(sig: function_signature) struct function_id: { }
    // typeof(function_id) = typeof(template(sig: function_signature) => function_id{sig})
    //                     = template_id{template(sig: function_signature) -> typeof(function_id{sig})}
    //                     = template_id{template(sig: function_signature) -> type}
    {
        auto* prog = cast<StructProgram>(program(builtins::function_signature_type.program()));
        VERIFY(!prog->isDependent());
        VERIFY(prog->members.empty());
    }
    {
        auto* prog = cast<StructProgram>(program(builtins::function_id_template.program()));
        VERIFY(prog->parameters.size() == 1);
        VERIFY(prog->parameters[0].name == parse::words["sig"]);
        VERIFY(prog->parameters[0].type == builtins::function_signature_type);
        VERIFY(!prog->parameters[0].defaultValue.has_value());
        VERIFY(prog->members.empty());
    }

    // typeof(tempalte(T: type) => expr) = template_id{template(T: type) -> typeof(expr)}
    // cast{type}(template(T: type) -> type_expr) = template_id{template(T: type) -> type_expr}

    // template(sig: template_signature) struct template_id: { }
    // typof(template_id) = typeof(template(sig: template_signature) => template_id{sig})
    //                    = template_id{template(sig: template_signature) -> typeof(template_id{sig})}
    //                    = template_id{template(sig: template_signature) -> type}
    {
        auto* prog = cast<StructProgram>(program(builtins::template_signature_type.program()));
        VERIFY(!prog->isDependent());
        VERIFY(prog->members.empty());
    }
    {
        auto* prog = cast<StructProgram>(program(builtins::template_id_template.program()));
        VERIFY(prog->parameters.size() == 1);
        VERIFY(prog->parameters[0].name == parse::words["sig"]);
        VERIFY(prog->parameters[0].type == builtins::template_signature_type);
        VERIFY(!prog->parameters[0].defaultValue.has_value());
        VERIFY(prog->members.empty());
    }

    // template(parent_type: type, member_type: type) struct member_ptr: { }
    {
        auto* prog = cast<StructProgram>(program(builtins::member_ptr_template.program()));
        VERIFY(prog->parameters.size() == 2);
        VERIFY(prog->parameters[0].name == parse::words["parent_type"]);
        VERIFY(prog->parameters[0].type == builtins::type_type);
        VERIFY(!prog->parameters[0].defaultValue.has_value());
        VERIFY(prog->parameters[1].name == parse::words["member_type"]);
        VERIFY(prog->parameters[1].type == builtins::type_type);
        VERIFY(!prog->parameters[1].defaultValue.has_value());
        VERIFY(prog->members.empty());
    }

    // template(T: type) fn copy(from: const shared T) -> T: { }
    {
        auto* prog = cast<FunctionProgram>(program(builtins::copy_function.program()));
        VERIFY(prog->parameterizes.size() == 1);
        VERIFY(prog->parameters[0].name == parse::words["T"]);
        VERIFY(prog->parameters[0].type == builtins::type_type);
        VERIFY(!prog->parameters[0].defaultValue.has_value());

        VERIFY(prog->functionParameters.size() == 1);
        VERIFY(prog->functionParameters[0].name() == parse::words["from"]);
        VERIFY(prog->functionParameters[0].type() == Constant(ConstantKind::CopyOfParameter, 0));
        VERIFY(prog->functionParameters[0].category().kind() == VariableKind::ConstSharedReference);
    }

    // cast{template_id}( template_function_id{template(T: type) fn(t: T) -> T)} )
    //   = template_id{ template(T: type) -> function_id{fn(t: T) -> T} }

    // template(T: type) fn(t: T) -> T = template(T: type) -> function_id{fn(t: T) -> T}

    // typeof( (arg = expr) ) = cast{type}( (arg = typeof(expr)) ) = tuple{cast{tuple_signature}( (arg = typeof(expr)) )}
}

}