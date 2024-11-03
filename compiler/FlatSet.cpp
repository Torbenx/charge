#include <FlatSet.h>

#include <gtest/gtest.h>

namespace {
[[maybe_unused]] void dumpSet(const FlatSet<int>& set) {
    std::string s;
    for (int i : set) {
        if (!s.empty())
            s += ", ";
        s += std::to_string(i);
    }
    fmt::println("{}", s);
}
}

TEST(FlatSet, Basic) {
    FlatSet<int> empty;
    EXPECT_EQ(empty.size(), 0);
    EXPECT_EQ(empty.capacity(), 0);

    auto set123 = FlatSet<int>::fromSorted(std::array { 1, 2, 3 });
    EXPECT_EQ(set123.size(), 3);
    EXPECT_GE(set123.capacity(), 3);

    auto set = set123;
    set.unionWith(empty);
    EXPECT_EQ(set, set123);
    set.unionWith(empty);
    EXPECT_EQ(set, set123);
    set.unionWith(empty);
    EXPECT_EQ(set, set123);
    set.unionWith(empty);
    EXPECT_EQ(set, set123);

    set.unionWith(set123);
    EXPECT_EQ(set, set123);
    set.unionWith(set123);
    EXPECT_EQ(set, set123);
    set.unionWith(set123);
    EXPECT_EQ(set, set123);
    set.unionWith(set123);
    EXPECT_EQ(set, set123);

    auto set456 = FlatSet<int>::fromSorted(std::array { 4, 5, 6 });
    EXPECT_EQ(set456.size(), 3);
    EXPECT_GE(set456.capacity(), 3);

    auto set123456 = FlatSet<int>::fromSorted(std::array { 1, 2, 3, 4, 5, 6 });
    EXPECT_EQ(set123456.size(), 6);
    EXPECT_GE(set123456.capacity(), 6);

    set.unionWith(set456);
    EXPECT_EQ(set, set123456);
    set.unionWith(set123);
    EXPECT_EQ(set, set123456);

    set.intersectionWith(set123);
    EXPECT_EQ(set, set123);
    set.intersectionWith(set123);
    EXPECT_EQ(set, set123);
    set.intersectionWith(set456);
    EXPECT_EQ(set, empty);

    set.intersectionWith(set123456);
    EXPECT_EQ(set, empty);
    set.unionWith(set123456);
    EXPECT_EQ(set, set123456);
    set.intersectionWith(empty);
    EXPECT_EQ(set, empty);

    auto set135 = FlatSet<int>::fromSorted(std::array { 1, 3, 5 });
    EXPECT_EQ(set135.size(), 3);
    EXPECT_GE(set135.capacity(), 3);

    auto set246 = FlatSet<int>::fromSorted(std::array { 2, 4, 6 });
    EXPECT_EQ(set246.size(), 3);
    EXPECT_GE(set246.capacity(), 3);

    set = set123456;
    set.intersectionWith(set135);
    EXPECT_EQ(set, set135);
    set.intersectionWith(set246);
    EXPECT_EQ(set, empty);
    set.unionWith(set246);
    EXPECT_EQ(set, set246);
    set.unionWith(set135);
    EXPECT_EQ(set, set123456);

    set = {};
    EXPECT_EQ(set, empty);
    set.add(1);
    set.add(5);
    set.add(3);
    EXPECT_EQ(set, set135);
    set.add(1);
    EXPECT_EQ(set, set135);
    set.add(6);
    set.add(2);
    set.add(4);
    EXPECT_EQ(set, set123456);
}