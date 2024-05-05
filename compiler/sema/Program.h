#pragma once

#include <glue/DeclarationNode.h>
#include <parse/Output.h>
#include <sema/Node.h>
#include <sema/Value.h>

namespace glue {

struct Context;

}

namespace sema {

namespace builtins {

#define BUILTIN_TYPE(name) constexpr inline Type name##_type { BuiltinId::name##_type };
#define BUILTIN(name, cppName) constexpr inline Value cppName { BuiltinId::cppName };
#include <sema/builtins.inc>

};

struct NodeHandle {
    NodeHandle(Node* node)
        : m_node(node) { VERIFY(node != nullptr); }

    Node* node() const { return m_node; }
    Node* operator->() const { return node(); }
    operator Node*() const { return node(); }
    NodeKind kind() const { return node()->kind(); }
    SourceLocation location() const { return node()->location(); }
    int_t childrenCount() const { return node()->childrenCount(); }
    ChildrenRange reverseChildren() const { return node()->reverseChildren(); }

    Node* m_node;
};

struct Expression : NodeHandle {
    Expression(Node* node)
        : NodeHandle(node) { VERIFY(isExpression(kind())); }
    NodeCategory category() const { return nodeCategory(kind()); }
    Type type() const { return node()->u.expr.type; }
    ExprData data() const { return node()->u.expr.u; }
};

enum class ProgramStatus : uint8_t {
    Unchecked,
    SignatureCheckInProgress,
    SignatureChecked, // (template) parameters have been checked, the type has been determined
};

enum class ProgramKind : uint8_t {
    Value,
    Type,
    Function,
};

#define ENUMERATE_PROGRAM_OPS               \
    PROGRAM_OP(TemplateSignature)         \
    PROGRAM_OP(FunctionSignature)         \
    PROGRAM_OP(NamespaceLiteral)            \
    PROGRAM_OP(RemoteExpression)            \
    PROGRAM_OP(Expression)                  \
    PROGRAM_OP(Parameterize)

struct Program {
    enum class Opcode : uint8_t {
#define PROGRAM_OP(kind) kind,
        ENUMERATE_PROGRAM_OPS
#undef PROGRAM_OP
    };
    struct Constant {
        Opcode op;
        union {
            uint64_t data;
            glue::DeclarationNode* declarationNode;
            uint32_t expressionIndex; // offset into expressions
            ProgramHandle templateSignature;
            Value functionSignature; // either a program or a parameterize constant
            struct {
                Value base; // either a program or a parameterize constant
                uint32_t expressionIndex; // offset into the target programs expressions
            } remoteExpression;
            struct {
                ProgramHandle base; // always a dependent program literal
                uint16_t firstArgumentIndex; // offset into parameterizeArguments
                uint16_t argumentCount;
            } parameterize;
        } u;
    };
    static_assert(sizeof(Constant) == 16);

    struct Parameter {
        Word name;
        ExternValue type;
        std::optional<Value> defaultValue;

        bool implicit() const { return name.empty(); }
    };

    struct ParameterizeArgumentSetter {
        Program* program = nullptr;
        int_t firstIndex = 0;

        void set(int_t index, Value value) {
            program->parameterizeArguments[firstIndex + index] = value;
        }
    };

    int_t importNode(Node* node);
    Value add(Constant);
    Value addNamespaceLiteral(glue::DeclarationNode* decl);
    Value addTemplateSignature(ProgramHandle prog);
    Value addFunctionSignature(Value base);
    Value addExpression(Node* expr);
    Value addRemoteExpression(Value base, uint32_t expressionIndex);
    Value addParameterize(ProgramHandle base, int_t firstArgumentIndex, int_t argumentCount);
    Value addParameterize(ProgramHandle base, std::span<const Value> arguments);

    // type after substituitng template arguments
    ExternValue type() const { return m_type.value(); }

    ExternValue parent() const { return m_parent.value(); }

    ExternValue self() const { return m_self.value(); }

    void setType(Type type) {
        VERIFY(!m_type.has_value());
        m_type = type;
    }
    void setParent(Value value) {
        VERIFY(!m_parent.has_value());
        m_parent = value;
    }
    void setSelf(Value value) {
        VERIFY(!m_self.has_value());
        m_self = value;
    }

    Word name() const { return m_name; }
    ProgramStatus status() const { return m_status; }
    void setStatus(ProgramStatus status) { m_status = status; }
    ProgramKind kind() const { return m_kind; }
    bool isDependent() const {
        return !parameters.empty();
    }
    bool isTemplate() const {
        return parameters.size() > inheritedParameterCount;
    }

    void dump(glue::Context&);

public:
    ProgramStatus m_status = ProgramStatus::Unchecked;
    ProgramKind m_kind = ProgramKind::Value;
    uint16_t inheritedParameterCount = 0;
    Word m_name;

    std::vector<Parameter> parameters;
    // Note: Typically references to constants should be avoided
    //       because they become invalid when this vector grows in capacity.
    std::vector<Constant> constants;
    std::vector<Node> expressions;
    std::vector<Value> parameterizeArguments;

    const ProgramHandle* programTranslationBuffer = nullptr;

protected:
    static constexpr uint32_t INVALID_SUBCLASS_DATA = -1;

    std::optional<ExternValue> m_type;
    uint32_t m_subClassData = INVALID_SUBCLASS_DATA;
    std::optional<ExternValue> m_parent;
    std::optional<ExternValue> m_self;

    friend struct Dumper;
};
static_assert(sizeof(Program) == 128);

struct ValueProgram : Program {
    void setValue(Value value) {
        VERIFY(m_subClassData == INVALID_SUBCLASS_DATA);
        m_subClassData = value.toUint();
    }

    ExternValue value() const {
        VERIFY(m_subClassData != INVALID_SUBCLASS_DATA);
        return Value::fromUint(m_subClassData);
    }
};

struct FunctionProgram : Program {
    struct FunctionParameter {
        Word name;
        Type type;
    };

    void setBody(Node* node) {
        VERIFY(m_subClassData == INVALID_SUBCLASS_DATA);
        m_subClassData = importNode(node);
    }
    Node* body() {
        VERIFY(m_subClassData != INVALID_SUBCLASS_DATA);
        return &expressions[m_subClassData];
    }

    std::vector<FunctionParameter> functionParameters;
};

struct TypeProgram : Program {
    struct Member { };
    std::vector<Member> members;
};

union ProgramUnion {
    ValueProgram value;
    FunctionProgram function;
    TypeProgram type;

    ProgramUnion(ProgramKind kind) {
        switch (kind) {
        case ProgramKind::Value:
            std::construct_at(&value);
            break;
        case ProgramKind::Function:
            std::construct_at(&function);
            break;
        case ProgramKind::Type:
            std::construct_at(&type);
            break;
        default:
            VERIFY_NOT_REACHED();
        }
        value.m_kind = kind;
    }

    Program& get() { return value; }

    ~ProgramUnion() {
        switch (value.kind()) {
        case ProgramKind::Value:
            std::destroy_at(&value);
            break;
        case ProgramKind::Function:
            std::destroy_at(&function);
            break;
        case ProgramKind::Type:
            std::destroy_at(&type);
            break;
        default:
            VERIFY_NOT_REACHED();
        }
    }
};
static_assert(sizeof(ProgramUnion) == 152);

std::string_view nameString(Program::Opcode op);

}