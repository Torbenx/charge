#include <padded_string.h>

#include <gtest/gtest.h>

namespace {

TEST(PaddedString, Content) {
    padded_string buffer("static a = a;");
    EXPECT_EQ(buffer.size(), 13);
    EXPECT_FALSE(buffer.empty());
    EXPECT_EQ(std::string_view(buffer), "static a = a;");
    EXPECT_EQ(std::string(buffer), "static a = a;");
    EXPECT_EQ(buffer.front(), 's');
    EXPECT_EQ(buffer.back(), ';');
    EXPECT_EQ(buffer[0], 's');
    EXPECT_EQ(std::string_view(buffer.begin(), buffer.end()), "static a = a;");

    padded_string empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.size(), 0);
    EXPECT_EQ(std::string_view(empty), "");
}

TEST(PaddedString, Padding) {
    for (int_t length = 0; length <= 40; length++) {
        padded_string buffer(std::string(length, 'x'));
        ASSERT_EQ(buffer.size(), length);
        // The content is terminated and the whole padding is readable and zero
        for (int_t i = 0; i < PADDED_STRING_PADDING; i++)
            EXPECT_EQ(buffer.data()[length + i], '\0') << length << "+" << i;
    }
}

TEST(PaddedString, CopyAndMove) {
    padded_string original("some source");
    padded_string copy(original);
    EXPECT_EQ(copy, original);
    EXPECT_NE(copy.data(), original.data());

    const char* originalData = original.data();
    padded_string moved(std::move(original));
    EXPECT_EQ(moved.data(), originalData);
    EXPECT_EQ(moved, copy);

    padded_string assigned;
    assigned = copy;
    EXPECT_EQ(assigned, copy);
    EXPECT_NE(assigned.data(), copy.data());

    assigned = std::move(moved);
    EXPECT_EQ(assigned.data(), originalData);
}

TEST(PaddedString, Comparison) {
    padded_string buffer("keyword");
    padded_string same("keyword");
    padded_string other("keyworf");

    EXPECT_TRUE(buffer == same);
    EXPECT_FALSE(buffer == other);
    EXPECT_TRUE(buffer != other);
    EXPECT_TRUE(buffer == "keyword");
    EXPECT_TRUE("keyword" == buffer);
    EXPECT_TRUE(buffer == std::string_view("keyword"));

    padded_string_view view(buffer);
    EXPECT_TRUE(view == padded_string_view(same));
    EXPECT_FALSE(view == padded_string_view(other));
    EXPECT_TRUE(view == std::string_view("keyword"));
    EXPECT_TRUE(std::string_view("keyword") == view);
    EXPECT_TRUE(view == buffer);
}

TEST(PaddedString, View) {
    padded_string buffer("fn f(): { return a; }");
    padded_string_view view(buffer);
    EXPECT_EQ(view.size(), buffer.size());
    EXPECT_EQ(view.data(), buffer.data());
    EXPECT_EQ(std::string_view(view), std::string_view(buffer));

    padded_string_view tail = view.substr(10);
    EXPECT_EQ(std::string_view(tail), "return a; }");
    EXPECT_EQ(std::string_view(view.substr(10, 6)), "return");
    // A substring still sits inside the padding of its buffer
    EXPECT_EQ(std::string_view(tail.substr(tail.size())), "");
    EXPECT_TRUE(view.substr(10, 6) == std::string_view("return"));

    EXPECT_TRUE(padded_string_view().empty());
    EXPECT_TRUE(padded_string_view() == padded_string_view());
    EXPECT_TRUE(padded_string_view::from_raw_unsafe(std::string_view(buffer)) == view);
}

}
