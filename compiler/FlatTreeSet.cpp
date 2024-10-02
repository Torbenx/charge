#include <FlatTreeSet.h>

#include <gtest/gtest.h>

namespace {

struct TestSet : FlatTreeSetDetail::Base<TestSet, uint32_t> {
    uint32_t get(uint32_t data) {
        return Base::get(data);
    }

private:
    friend Base;
    uint32_t makeNode(uint32_t data, TreeLabel label) { return Base::makeNode(label, data); }
    std::strong_ordering compare(uint32_t a, uint32_t b) { return a <=> b; }
};

struct ArrayTestSet : FlatTreeSetDetail::Base<ArrayTestSet, uint32_t, float> {
    uint32_t get(uint32_t data) {
        return Base::get(data);
    }

    const uint32_t& at(uint32_t handle) const { return Base::at(handle).first; }

private:
    friend Base;
    uint32_t makeNode(uint32_t data, TreeLabel label) { return Base::makeNode(label, data, std::span<const float>({ 1.0f, 2.0f })); }
    std::strong_ordering compare(uint32_t a, uint32_t b) { return a <=> b; }
};

void testFlatSet(auto set) {
    auto index10 = set.get(10);
    EXPECT_EQ(set.at(index10), 10);
    EXPECT_EQ(set.get(10), index10);

    auto index11 = set.get(11);
    EXPECT_EQ(set.at(index11), 11);
    EXPECT_EQ(set.get(11), index11);

    auto index9 = set.get(9);
    EXPECT_EQ(set.at(index9), 9);
    EXPECT_EQ(set.get(9), index9);

    EXPECT_EQ(set.at(index10), 10);
    EXPECT_EQ(set.at(index11), 11);
    EXPECT_EQ(set.at(index9), 9);
}

}


TEST(FlatTreeSet, Basic) {
    testFlatSet(TestSet());
    testFlatSet(ArrayTestSet());
}

TEST(TreeLabel, Basic) {
    TreeLabel root = TreeLabel::rootLabel();
    EXPECT_EQ(root.label(), 0b0111'1111'1111'1111'1111'1111'1111'1111u);

    EXPECT_EQ(root.extend(false).label(), 0b0011'1111'1111'1111'1111'1111'1111'1111u);
    EXPECT_EQ(root.extend(false).extend(false).label(), 0b0001'1111'1111'1111'1111'1111'1111'1111u);
    EXPECT_EQ(root.extend(false).extend(true).label(), 0b0101'1111'1111'1111'1111'1111'1111'1111u);

    EXPECT_EQ(root.extend(true).label(), 0b1011'1111'1111'1111'1111'1111'1111'1111u);
    EXPECT_EQ(root.extend(true).extend(false).label(), 0b1001'1111'1111'1111'1111'1111'1111'1111u);
    EXPECT_EQ(root.extend(true).extend(true).label(), 0b1101'1111'1111'1111'1111'1111'1111'1111u);

    EXPECT_EQ(root.depth(), 0);

    EXPECT_EQ(root.extend(false).depth(), 1);
    EXPECT_EQ(root.extend(false).extend(false).depth(), 2);
    EXPECT_EQ(root.extend(false).extend(true).depth(), 2);

    EXPECT_EQ(root.extend(true).depth(), 1);
    EXPECT_EQ(root.extend(true).extend(false).depth(), 2);
    EXPECT_EQ(root.extend(true).extend(true).depth(), 2);
}