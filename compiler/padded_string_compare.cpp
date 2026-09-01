#include <padded_string_compare.h>

#include <gtest/gtest.h>

#include <string>

namespace {

//! A buffer holding \p content followed by readable padding filled with \p paddingFill
/*!
The padding is deliberately given a different filler in the two operands of every comparison,
so a test fails as soon as an implementation lets the padding influence the result.
*/
struct PaddedBuffer {
    PaddedBuffer(std::string_view content, char paddingFill)
        : storage(content) {
        storage.append(PADDED_STRING_PADDING, paddingFill);
    }

    const char* data() const { return storage.data(); }

    std::string storage;
};

std::string repeatedPattern(int_t length) {
    std::string result;
    for (int_t i = 0; i < length; i++)
        result.push_back(static_cast<char>('a' + i % 26));
    return result;
}

TEST(PaddedStringCompare, Equal) {
    for (int_t length = 0; length <= 64; length++) {
        std::string content = repeatedPattern(length);
        PaddedBuffer a(content, '\0');
        PaddedBuffer b(content, '?');
        EXPECT_TRUE(padded_string_compare_eq(a.data(), b.data(), length)) << length;
    }
}

TEST(PaddedStringCompare, SingleCharacterDifference) {
    for (int_t length = 1; length <= 64; length++) {
        std::string content = repeatedPattern(length);
        PaddedBuffer a(content, '\0');
        for (int_t index = 0; index < length; index++) {
            std::string modified = content;
            modified[index] += 1;
            PaddedBuffer b(modified, '?');
            EXPECT_FALSE(padded_string_compare_eq(a.data(), b.data(), length)) << length << "@" << index;
        }
    }
}

TEST(PaddedStringCompare, PaddingIsIgnored) {
    // The characters behind the compared length differ in every way the padding allows
    for (int_t length = 0; length <= 33; length++) {
        std::string content = repeatedPattern(length);
        PaddedBuffer a(content, 'x');
        PaddedBuffer b(content, 'y');
        EXPECT_TRUE(padded_string_compare_eq(a.data(), b.data(), length)) << length;
    }
}

TEST(PaddedStringCompare, Small) {
    for (int_t length = 0; length <= PADDED_STRING_PADDING; length++) {
        std::string content = repeatedPattern(length);
        PaddedBuffer a(content, 'x');
        PaddedBuffer b(content, 'y');
        EXPECT_TRUE(padded_small_string_compare_eq(a.data(), b.data(), length)) << length;
        EXPECT_EQ(padded_small_string_compare_eq(a.data(), b.data(), length),
            padded_string_compare_eq(a.data(), b.data(), length));

        for (int_t index = 0; index < length; index++) {
            std::string modified = content;
            modified[index] += 1;
            PaddedBuffer different(modified, 'y');
            EXPECT_FALSE(padded_small_string_compare_eq(a.data(), different.data(), length))
                << length << "@" << index;
        }
    }
}

// Constant evaluation goes through the scalar fallback, which the word tables rely on
static_assert(padded_small_string_compare_eq("static", "static", 6));
static_assert(!padded_small_string_compare_eq("static", "statir", 6));
static_assert(padded_small_string_compare_eq("static", "staticXXX", 6));
static_assert(padded_string_compare_eq("a rather long constant string", "a rather long constant string", 29));
static_assert(!padded_string_compare_eq("a rather long constant string", "a rather long constant strinX", 29));
static_assert(padded_string_compare_eq("", "", 0));

}
