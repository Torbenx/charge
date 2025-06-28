#pragma once

#include <types.h>
#include <check/Value.h>

namespace check {

struct ConcreteTypeHandle {
    uint32_t id;
};

struct TreeIndex {
    uint16_t forwardIndex;
    uint16_t backwardIndex;

    std::partial_ordering operator<=>(const TreeIndex& other) const {
        auto fo = forwardIndex <=> other.forwardIndex;
        auto bo = backwardIndex <=> other.backwardIndex;
        if (fo == bo)
            return fo;
        return std::partial_ordering::unordered;
    }
    bool operator==(const TreeIndex& other) const {
        return forwardIndex == other.forwardIndex && backwardIndex == other.backwardIndex;
    }
};

struct ConcreteType {

    struct ContainedType {
        ConcreteTypeHandle type;
        uint16_t instanceOffset;
        uint16_t instanceCount;
    };

    std::span<const ContainedType> memberTypes() const { return m_memberTypes; }
    std::span<const ContainedType> hasMemberTypes() const { return m_hasMemberTypes; }
    ConcreteTypeHandle typeAt(TreeIndex index) const { return forwardMembers[index.forwardIndex]; }
    std::span<const TreeIndex> instancesOf(ContainedType ct) const {
        return { typeInstances.data() + ct.instanceOffset, ct.instanceCount };
    }

    std::vector<ContainedType> m_memberTypes;
    std::vector<ContainedType> m_hasMemberTypes;
    std::vector<ConcreteTypeHandle> forwardMembers;
    std::vector<TreeIndex> typeInstances;
};

struct GenericType {
    std::vector<Type> m_directMemberTypes;
    std::vector<Type> m_directHasMemberTypes;
};

}