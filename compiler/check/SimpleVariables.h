#pragma once

#include <check/MemoryLocations.h>

namespace check {

struct SimpleVariables : MemoryLocationTheory {
    SimpleVariables(Solver& solver)
        : MemoryLocationTheory(solver), baseLabel(solver, ValueCategory::Literal), declarations(solver) { }

    MemoryLocation declareVariable(Solver& solver, Type type, CodePosition position);

    MemoryDeclaration memoryDeclaration(Solver&, MemoryLocation value) override {
        return { (uint32_t)declarations.theoryId(), value.valueId };
    }

    MemberExpression memberExpression(Solver&, MemoryLocation value) override {
        return variables[value.valueId].identityExpressionForType;
    }

    void collectValueInactiveReasons(Solver& solver, Value v, std::vector<BooleanValue>& clause) override {
        solver.collectInactiveReasons(solver.negate(solver.blockActiveLiteral(variables[v.valueId].position.block)), clause);
    }

    bool isValueActive(Solver& solver, Value v) override {
        return solver.assignedTrue(solver.blockActiveLiteral(variables[v.valueId].position.block));
    }

    uint64_t labelOfValue(Solver&, Value v) override {
        return baseLabel + (uint64_t)v.valueId;
    }

    Type typeAtLocation(Solver&, MemoryLocation loc) override {
        return variables[loc.valueId].type;
    }

    std::string formatValue(Solver&, Value v) override {
        return "var" + std::to_string(v.valueId);
    }

private:
    struct VariableInfo : MemoryDeclarationTheory::DeclarationInfo {
        MemberExpression identityExpressionForType;
    };

    struct Declarations : MemoryDeclarationTheory {
        Declarations(Solver& solver)
            : MemoryDeclarationTheory(solver), baseLabel(solver, ValueCategory::Literal) { }

        SimpleVariables* theory() {
            return ReverseMemberPointer<&SimpleVariables::declarations>::reverse(this);
        }

        std::optional<DeclarationInfo> declarationInfo(Solver&, MemoryDeclaration v) override {
            return theory()->variables[v.valueId];
        }

        uint64_t labelOfValue(Solver&, Value v) override {
            return baseLabel + (uint64_t)v.valueId;
        }

        std::string formatValue(Solver&, Value v) override {
            return "var" + std::to_string(v.valueId) + ".decl";
        }

        void collectValueInactiveReasons(Solver& solver, Value v, std::vector<BooleanValue>& clause) override {
            // This is fine since the theory id of 'v' is not used
            return theory()->collectValueInactiveReasons(solver, v, clause);
        }

        bool isValueActive(Solver& solver, Value v) override {
            // This is fine since the theory id of 'v' is not used
            return theory()->isValueActive(solver, v);
        }

        ValueBaseLabel baseLabel;
    };

    std::vector<VariableInfo> variables;
    ValueBaseLabel baseLabel;
    Declarations declarations;
};

}