#include <types.h>
#include <verify/z3.h>

#include <gtest/gtest.h>

#include <array>
#include <utility>

TEST(Verify, Z3) {
    z3::context ctx;
    z3::solver solver(ctx);

    z3::expr x = ctx.bool_const("x");
    z3::expr y = ctx.bool_const("y");
    z3::expr conjecture = (!(x && y)) == (!x || !y);

    solver.add(!conjecture);
    EXPECT_EQ(solver.check(), z3::unsat);
}

enum class ScalarKind : uint8_t {
    Bool,
    Type,
    Member,
    MemoryDecl,
};

enum class ExprKind : uint8_t {
#define EXPR(kind, ...) kind,
#include <verify/expressions.inc>
};

struct Expr {
    Expr(ExprKind kind, uint32_t id)
        : kindBits(std::to_underlying(kind)), idBits(id) { }

    ExprKind kind() const { return (ExprKind)kindBits; }
    uint32_t id() const { return idBits; }
    uint32_t kindBits : 7 = 0;
    uint32_t boolNegatedBit : 1 = 0;
    uint32_t idBits : 24 = 0;
};

struct Bool : Expr {
    explicit Bool(Expr e)
        : Expr(e) { }
    explicit Bool(bool literal)
        : Expr(ExprKind::FalseLiteral, 0) { boolNegatedBit = literal; }

    Bool operator!() const {
        Bool copy = *this;
        copy.boolNegatedBit = !boolNegatedBit;
        return copy;
    }
};

struct Type : Expr {
    explicit Type(Expr e)
        : Expr(e) { }
};
struct MemoryDecl : Expr {
    explicit MemoryDecl(Expr e)
        : Expr(e) { }
};
struct Member : Expr {
    explicit Member(Expr e)
        : Expr(e) { }
};

struct TypeDecl {
    explicit TypeDecl(uint32_t id)
        : m_id(id) { }
    uint32_t id() const { return m_id; }
    uint32_t m_id;
};

struct MemoryLoc {
    MemoryDecl decl;
    Member member;
};

struct CodePos {
    uint32_t instruction;
};

struct ExprList {
    uint32_t m_offset = 0;
    uint32_t m_size = 0;

    bool empty() const { return m_size == 0; }
};

struct ExpressionStorage {
private:
    using arr = std::array<uint32_t, 3>;

    template<typename T>
    static arr toArray(const T& in) {
        if constexpr (sizeof(T) == 3 * sizeof(uint32_t)) {
            return std::bit_cast<arr>(in);
        } else if constexpr (sizeof(T) == 2 * sizeof(uint32_t)) {
            auto tmp = std::bit_cast<std::array<uint32_t, 2>>(in);
            return arr { tmp[0], tmp[1], 0u };
        } else {
            auto tmp = std::bit_cast<std::array<uint32_t, 1>>(in);
            return arr { tmp[0], 0u, 0u };
        }
    }

    template<typename T>
    static T fromArray(const arr& in) {
        if constexpr (sizeof(T) == 3 * sizeof(uint32_t)) {
            return std::bit_cast<T>(in);
        } else if constexpr (sizeof(T) == 2 * sizeof(uint32_t)) {
            std::array<uint32_t, 2> tmp { in[0], in[1] };
            return std::bit_cast<T>(tmp);
        } else {
            std::array<uint32_t, 1> tmp { in[0] };
            return std::bit_cast<T>(tmp);
        }
    }

public:
    ExprList makeList(std::span<const Expr> list) {
        uint32_t offset = expressionLists.size();
        expressionLists.insert(expressionLists.end(), list.begin(), list.end());
        return { offset, (uint32_t)list.size() };
    }

    std::span<const Expr> view(ExprList list) const {
        VERIFY((int_t)list.m_offset + (int_t)list.m_size <= (int_t)expressionLists.size());
        return { expressionLists.data() + list.m_offset, list.m_size };
    }

#define EXPR_PARTS_CONCAT(a, b, c, ...) \
    a;                                  \
    b;                                  \
    c;
#define EXPR_STRUCT(...) EXPR_PARTS_CONCAT(__VA_ARGS__, , , , )
#define SIMPLE_EXPR(name, result)
#define EXPR(name, resultType, args...)                   \
    struct name##S {                                      \
        EXPR_STRUCT(args)                                 \
    };                                                    \
    name##S get##name(resultType r) const {               \
        VERIFY(r.kind() == ExprKind::name);               \
        return fromArray<name##S>(expressions[r.idBits]); \
    }                                                     \
    resultType add##name(const name##S& val) {            \
        uint32_t id = expressions.size();                 \
        expressions.push_back(toArray<name##S>(val));     \
        return (resultType)Expr(ExprKind::name, id);      \
    }
#include <verify/expressions.inc>

private:
    std::vector<arr> expressions;
    std::vector<Expr> expressionLists;
};

struct Problem : ExpressionStorage {
    struct TypeDeclaration {
        struct Member {
            std::string_view name;
        };

        struct Parameter {
            std::string_view name;
            ScalarKind kind;
        };

        struct Impl {
            std::vector<Member> members;
        };

        std::string_view name;
        std::vector<Parameter> parameters;
        std::vector<Impl> impls;
    };

    TypeDecl addTypeDecl(std::string_view name);
};

z3::expr z3forall(std::initializer_list<z3::expr> quant, z3::expr pred) {
    z3::expr_vector quantVec(pred.ctx());
    for (const auto& q : quant)
        quantVec.push_back(q);
    return z3::forall(quantVec, pred);
}

z3::expr z3forall(std::initializer_list<z3::expr> quant, z3::expr pred, std::initializer_list<z3::expr> multiPattern) {
    std::vector<Z3_app> quantVec;
    for (const auto& q : quant)
        quantVec.push_back(q);
    std::vector<Z3_ast> patternVec;
    for (const auto& p : multiPattern)
        patternVec.push_back(p);
    Z3_pattern patternHandle = Z3_mk_pattern(pred.ctx(), multiPattern.size(), patternVec.data());
    pred.check_error();
    auto r = Z3_mk_forall_const(pred.ctx(), 0, quant.size(), quantVec.data(), 1, &patternHandle, pred);
    pred.check_error();
    return { pred.ctx(), r };
}

z3::expr z3forall(std::initializer_list<z3::expr> quant, z3::expr pred, z3::expr pattern) {
    return z3forall(quant, pred, { pattern });
}

TEST(Verify, Z3member) {
    z3::context c;
    z3::solver s = { c, Z3_mk_simple_solver(c) };
    // s.set("smt.mbqi", false);

    auto typeSort = c.uninterpreted_sort("Type");
    auto mdeclSort = c.uninterpreted_sort("MemoryDecl");

    auto memberSort = c.uninterpreted_sort("Member");
    auto memberCompose = z3::function("MemberCompose", memberSort, memberSort, memberSort);
    {
        // Remove common prefix
        auto p = c.constant("parent", memberSort);
        auto c1 = c.constant("child1", memberSort);
        auto c2 = c.constant("child2", memberSort);
        auto pattern = memberCompose(p, c1) == memberCompose(p, c2);
        s.add(z3forall({ p, c1, c2 }, pattern == (c1 == c2), pattern));
    }
    {
        // Associative law
        auto m1 = c.constant("member1", memberSort);
        auto m2 = c.constant("member2", memberSort);
        auto m3 = c.constant("member3", memberSort);
        auto pattern = memberCompose(memberCompose(m1, m2), m3);
        s.add(z3forall({ m1, m2, m3 }, pattern == memberCompose(m1, memberCompose(m2, m3)), pattern));
    }

    {
        auto p1 = c.constant("parent1", memberSort);
        auto p2 = c.constant("parent2", memberSort);
        auto c1 = c.constant("child1", memberSort);
        auto c2 = c.constant("child2", memberSort);
        auto i1 = c.constant("intermediate1", memberSort);
        auto i2 = c.constant("intermediate2", memberSort);
        auto pattern = memberCompose(p1, c1) == memberCompose(p2, c2);
        auto case1 = z3::exists(i1, memberCompose(p1, i1) == p2); // && c1 == memberCompose(leftExt(c1, c2), c2);
        auto case2 = z3::exists(i2, memberCompose(p2, i2) == p1); // && c2 == memberCompose(leftExt(c2, c1), c1);
        s.add(z3forall({ p1, p2, c1, c2 }, z3::implies(pattern, case1 || case2), pattern));
    }

    {
        auto m1 = c.constant("member1", memberSort);
        auto i = c.constant("intermediate", memberSort);
        s.add(z3forall({ m1 }, z3::exists(i, memberCompose(m1, i) == m1)));
    }

    auto isId = z3::function("isidentity", memberSort, c.bool_sort());
    {
        auto m1 = c.constant("member1", memberSort);
        auto m2 = c.constant("member2", memberSort);
        auto pattern = memberCompose(m1, m2) == m1;
        s.add(z3forall({ m1, m2 }, pattern == isId(m2), pattern));
        s.add(z3forall({ m1, m2 }, isId(m2) == pattern, { memberCompose(m1, m2), isId(m2) }));
    }

    {
        auto m1 = c.constant("member1", memberSort);
        auto m2 = c.constant("member2", memberSort);
        auto pattern = isId(memberCompose(m1, m2));
        s.add(z3forall({ m1, m2 }, pattern == (isId(m1) && isId(m2))), pattern);
    }

    auto memberContains = z3::function("MemberContains", memberSort, memberSort, c.bool_sort());
    {
        auto m1 = c.constant("member1", memberSort);
        auto m2 = c.constant("member2", memberSort);
        auto i = c.constant("intermediate", memberSort);
        auto pattern = memberContains(m1, m2);
        s.add(z3forall({ m1, m2 }, pattern == z3::exists(i, m2 == memberCompose(m1, i)), pattern));
    }

    auto m1 = c.constant("m1", memberSort);
    auto m2 = c.constant("m2", memberSort);
    auto m3 = c.constant("m3", memberSort);
    auto m4 = c.constant("m4", memberSort);

    println("reflexive:");
    s.push();
    s.add(!memberContains(m1, m1));
    EXPECT_EQ(s.check(), z3::unsat);
    s.pop();

    println("antisymmetric:");
    s.push();
    s.add(memberContains(m1, m2) && memberContains(m2, m1));
    s.add(m1 != m2);
    EXPECT_EQ(s.check(), z3::unsat);
    s.pop();

    println("transitive:");
    s.push();
    s.add(memberContains(m1, m2) && memberContains(m2, m3));
    s.add(!memberContains(m1, m3));
    EXPECT_EQ(s.check(), z3::unsat);
    s.pop();

    println("tree:");
    s.push();
    s.add(memberContains(m1, m3) && memberContains(m2, m3));
    s.add(!memberContains(m1, m2) && !memberContains(m2, m1));
    EXPECT_EQ(s.check(), z3::unsat);
    s.pop();

    println("test:");
    s.add(memberContains(memberCompose(memberCompose(m1, m2), m3), memberCompose(memberCompose(m1, m2), m4)));
    // s.add(!memberContains(memberCompose(m1, memberCompose(m2, m3)), memberCompose(m1, memberCompose(m2, m4))));
    auto q = c.constant("q", memberSort);
    // s.add(z3forall({ q }, memberCompose(memberCompose(m1, memberCompose(m2, m3)), q) != memberCompose(m1, memberCompose(m2, m4))));
    // s.add(z3forall({ q }, memberCompose(m1, memberCompose(m2, memberCompose(m3, q))) != memberCompose(m1, memberCompose(m2, m4))));
    // s.add(z3forall({ q }, memberCompose(m2, memberCompose(m3, q)) != memberCompose(m2, m4)));
    // s.add(z3forall({ q }, memberCompose(m3, q) != m4));
    s.add(!memberContains(m3, m4));
    EXPECT_EQ(s.check(), z3::unsat);
}