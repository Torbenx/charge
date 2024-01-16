#include "semantic.h"

struct inst {
    Opcode opcode;
    InstructionOperand out;
};

struct unary_inst : inst {
    InstructionOperand in;

private:
    InstructionOperand unused;
};
struct binary_inst : inst {
    InstructionOperand left;
    InstructionOperand right;
};
struct foreign_constant_inst : inst {
    InstructionOperand baseDecl;
    InstructionOperand foreignName;
};
struct call_inst : inst {
    InstructionOperand base;
    uint16_t argumentCount;
    InstructionOperand firstArgument() const {
        return InstructionOperand(false, out.id() - argumentCount);
    }
    InstructionOperand lastArgument() const {
        return InstructionOperand(false, out.id() - 1);
    }
};
struct parameter_slot_inst : inst {
    uint16_t index;

private:
    InstructionOperand unused;
};

template<typename Impl>
struct InstructionVisitor {

    Impl* impl() { return static_cast<Impl*>(this); }

    void visit(InstructionStream& s) {
        for (int_t idx = 0; idx < (int_t)s.stream.size(); idx++) {
            Instruction inst = s.stream[idx];

            switch (inst.opcode()) {

#define INST(name, layout)                                          \
    case Opcode::name:                                              \
        impl()->visit_##layout(std::bit_cast<layout##_inst>(inst)); \
        break;
                ENUMERATE_INSTRUCTIONS
#undef INST

            default:
                VERIFY_NOT_REACHED();
            }
        }
    }
};

namespace {
struct FormattedOperand {
    std::array<char, 6> storage = {};
    uint8_t length = 0;

    operator std::string_view() const {
        return { storage.end() - length, storage.end() };
    }
};
}

std::ostream& operator<<(std::ostream& s, FormattedOperand op) {
    return s << (std::string_view)op;
}

namespace {

struct Dumper : InstructionVisitor<Dumper> {
    ValuePhase phase;
    static FormattedOperand format(InstructionOperand operand, ValuePhase phase) {
        uint16_t in = operand.id();
        FormattedOperand out = {};
        if (operand.constant())
            phase = (ValuePhase)(std::to_underlying(phase) - 1);
        char prefix = phase == ValuePhase::Literal ? 'L' : (phase == ValuePhase::Constant ? 'C' : 'R');

        do {
            char digit = '0' + in % 10;
            in /= 10;
            out.storage[out.storage.size() - ++out.length] = digit;
        } while (in != 0);
        out.storage[out.storage.size() - ++out.length] = prefix;

        return out;
    }
    FormattedOperand format(InstructionOperand operand) const {
        return format(operand, phase);
    }
    static FormattedOperand format(ConstantStreamOperand operand) {
        return format(operand, ValuePhase::Constant);
    }
    static FormattedOperand format(RuntimeStreamOperand operand) {
        return format(operand, ValuePhase::Runtime);
    }
    std::string_view instName(Opcode op) {
        switch (op) {

#define INST(name, layout) \
    case Opcode::name:     \
        return #name;
            ENUMERATE_INSTRUCTIONS
#undef INST

        default:
            VERIFY_NOT_REACHED();
        }
    }

    void printLHS(InstructionOperand op) {
        std::cout << "  " << format(op) << " = ";
    }
    void printBase(inst i) {
        printLHS(i.out);
        if (i.opcode != Opcode::Nop)
            std::cout << instName(i.opcode) << " ";
    }

    void visit_unary(unary_inst i) {
        printBase(i);
        std::cout << format(i.in) << '\n';
    }
    void visit_binary(binary_inst i) {
        printBase(i);
        std::cout << format(i.left) << ' ' << format(i.right) << '\n';
    }
    void visit_foreign_constant(foreign_constant_inst i) {
        printBase(i);
        std::cout << format(i.baseDecl) << ":" << format(i.foreignName, ValuePhase::Constant) << '\n';
    }
    void visit_call(call_inst i) {
        printBase(i);
        std::cout << format(i.base) << "(";
        if (i.argumentCount > 0)
            std::cout << format(i.firstArgument()) << ".." << format(i.lastArgument());
        std::cout << ")\n";
    }
    void visit_parameter_slot(parameter_slot_inst i) {
        printBase(i);
        std::cout << i.index << '\n';
    }

    void dump(InstructionStream& s) {
        phase = s.stream_phase;
        visit(s);
    }
    void dumpLiterals(DeclProgram& p, const WordStringTable& wordTable) {
        phase = ValuePhase::Literal;
        for (int_t i = 0; i < (int_t)p.encodedLiteralValues.size(); i++) {
            auto constant = p.literal(i);
            printLHS(InstructionOperand(false, i));
            switch (constant.type) {
            case ConstantType::Decl: {
                Decl* decl = constant.asDecl();
                std::cout << "(" << nameString(decl->kind()) << ") " << wordTable.view(decl->name) << '\n';
                break;
            }

            default:
                VERIFY_NOT_REACHED();
            }
        }
    }
};
}

void dumpIR(Decl* decl, const WordStringTable& wordTable) {
    auto paramDecl = dyn_cast<ParameterizedDecl>(decl);
    std::cout << wordTable.view(decl->name) << ":\n";
    auto* prog = dyn_cast<ParameterizedDecl>(decl)->program();
    for (auto param : prog->templateParameters) {
        std::cout << "  " << (param.implicit ? "implicit template" : "template");
        if (!param.name.empty())
            std::cout << " " << wordTable.view(param.name);
        std::cout << " @" << Dumper::format(param.slot) << ": " << Dumper::format(param.type) << '\n';
    }
    if (auto typeDecl = dyn_cast<TypeDecl>(decl)) {
        // Nothing to do
    } else if (auto varDecl = dyn_cast<StaticVariableDecl>(decl)) {
        auto* prog = varDecl->program();
        std::cout << "  type = " << Dumper::format(prog->type) << '\n';
        std::cout << "  value = " << Dumper::format(prog->value) << '\n';
        std::cout << '\n';
    } else if (auto fnDecl = dyn_cast<FunctionDecl>(decl)) {
        for (auto param : fnDecl->program()->runtimeParameters) {
            std::cout << "  " << [&] -> std::string_view {
                switch (param.model) {
                case ParameterModel::Let:
                    return "let";
                case ParameterModel::Var:
                    return "var";
                case ParameterModel::In:
                    return "in";
                case ParameterModel::InOut:
                    return "inout";
                case ParameterModel::Out:
                    return "out";
                default:
                    VERIFY_NOT_REACHED();
                }
            }();
            if (!param.name.empty())
                std::cout << " " << wordTable.view(param.name);
            std::cout << " @" << Dumper::format(param.slot) << ": " << Dumper::format(param.type) << '\n';
        }
        std::cout << "  return-slot @" << Dumper::format(fnDecl->program()->returnSlot);
        std::cout << ": " << Dumper::format(fnDecl->program()->returnType) << '\n';
        std::cout << '\n';
    }
    Dumper dumper;
    dumper.dumpLiterals(*paramDecl->program(), wordTable);
    std::cout << '\n';
    dumper.dump(paramDecl->program()->constantStream);
    std::cout << '\n';
    dumper.dump(paramDecl->program()->runtimeStream);
}