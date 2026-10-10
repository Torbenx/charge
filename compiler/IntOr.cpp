#include <IntOr.h>

#include <gtest/gtest.h>

#include <limits>

namespace {
template<typename I>
void expectIntRoundTrip(I value) {
    auto v = IntOr<I, bool>::forInt(value);
    EXPECT_TRUE(v.isInt());
    EXPECT_EQ(v.get(), value);
    EXPECT_EQ(v.getScaled(), static_cast<I>(static_cast<std::make_unsigned_t<I>>(value) << 1));
}

// Values that use all N-1 available bits of an N-bit integer type.
template<typename I>
void expectIntLimitsRoundTrip() {
    expectIntRoundTrip<I>(0);
    expectIntRoundTrip<I>(1);
    expectIntRoundTrip<I>(std::numeric_limits<I>::max() >> 1);
    if constexpr (std::is_signed_v<I>) {
        expectIntRoundTrip<I>(-1);
        expectIntRoundTrip<I>(std::numeric_limits<I>::min() >> 1);
    }
}

enum class Color : uint8_t {
    Red,
    Green,
    Blue,
};

struct ThreeBytes {
    uint8_t a, b, c;
};
}

TEST(IntOr, IntRoundTrip) {
    expectIntLimitsRoundTrip<int8_t>();
    expectIntLimitsRoundTrip<uint8_t>();
    expectIntLimitsRoundTrip<int16_t>();
    expectIntLimitsRoundTrip<uint16_t>();
    expectIntLimitsRoundTrip<int32_t>();
    expectIntLimitsRoundTrip<uint32_t>();
    expectIntLimitsRoundTrip<int64_t>();
    expectIntLimitsRoundTrip<uint64_t>();
}

TEST(IntOr, GetScaled) {
    EXPECT_EQ(Int32Or<uint8_t>::forInt(21).getScaled(), 42);
    EXPECT_EQ(Int32Or<uint8_t>::forInt(-21).getScaled(), -42);
    EXPECT_EQ(UInt32Or<uint8_t>::forInt(21).getScaled(), 42u);
}

TEST(IntOr, SmallPayload) {
    auto v = Int64Or<uint8_t>::forPayload(0xab);
    EXPECT_FALSE(v.isInt());
    EXPECT_EQ(v.payload(), 0xab);

    auto negative = Int64Or<int16_t>::forPayload(-1);
    EXPECT_FALSE(negative.isInt());
    EXPECT_EQ(negative.payload(), -1);

    auto color = Int32Or<Color>::forPayload(Color::Blue);
    EXPECT_FALSE(color.isInt());
    EXPECT_EQ(color.payload(), Color::Blue);

    auto bytes = Int32Or<ThreeBytes>::forPayload({ 1, 2, 0xff });
    EXPECT_FALSE(bytes.isInt());
    EXPECT_EQ(bytes.payload().a, 1);
    EXPECT_EQ(bytes.payload().b, 2);
    EXPECT_EQ(bytes.payload().c, 0xff);

    auto f = Int64Or<float>::forPayload(1.5f);
    EXPECT_FALSE(f.isInt());
    EXPECT_EQ(f.payload(), 1.5f);
}

TEST(IntOr, SameSizePayload) {
    // A payload as wide as the integer must leave the top bit free.
    auto zero = Int32Or<uint32_t>::forPayload(0);
    EXPECT_FALSE(zero.isInt());
    EXPECT_EQ(zero.payload(), 0u);

    auto five = Int32Or<uint32_t>::forPayload(5);
    EXPECT_FALSE(five.isInt());
    EXPECT_EQ(five.payload(), 5u);

    auto largest = Int32Or<uint32_t>::forPayload(0x7fffffff);
    EXPECT_FALSE(largest.isInt());
    EXPECT_EQ(largest.payload(), 0x7fffffffu);

    auto byte = UInt8Or<uint8_t>::forPayload(0x7f);
    EXPECT_FALSE(byte.isInt());
    EXPECT_EQ(byte.payload(), 0x7f);

    EXPECT_THROW(Int32Or<uint32_t>::forPayload(0x80000000), std::runtime_error);
    EXPECT_THROW(Int64Or<uint64_t>::forPayload(uint64_t(1) << 63), std::runtime_error);
    EXPECT_THROW(UInt8Or<uint8_t>::forPayload(0x80), std::runtime_error);
}

TEST(IntOr, PayloadSameTypeAsInt) {
    auto i = Int64Or<int64_t>::forInt(-5);
    EXPECT_TRUE(i.isInt());
    EXPECT_EQ(i.get(), -5);

    auto p = Int64Or<int64_t>::forPayload(5);
    EXPECT_FALSE(p.isInt());
    EXPECT_EQ(p.payload(), 5);
}

TEST(IntOr, PointerPayload) {
    alignas(8) static int64_t values[2] = { 1, 2 };

    auto p = Int64Or<int64_t*>::forPayload(&values[1]);
    EXPECT_FALSE(p.isInt());
    EXPECT_EQ(p.payload(), &values[1]);
    EXPECT_EQ(*p.payload(), 2);

    auto constP = Int64Or<const int64_t*>::forPayload(&values[0]);
    EXPECT_FALSE(constP.isInt());
    EXPECT_EQ(constP.payload(), &values[0]);

    auto null = Int64Or<int64_t*>::forPayload(nullptr);
    EXPECT_FALSE(null.isInt());
    EXPECT_EQ(null.payload(), nullptr);

    // Pointers to byte-aligned types may not have their lowest bit available.
    alignas(2) static char chars[2] = {};
    auto even = Int64Or<char*>::forPayload(&chars[0]);
    EXPECT_FALSE(even.isInt());
    EXPECT_EQ(even.payload(), &chars[0]);
    EXPECT_THROW(Int64Or<char*>::forPayload(&chars[1]), std::runtime_error);
}

TEST(IntOr, WrongAlternative) {
    auto i = Int64Or<uint8_t>::forInt(7);
    EXPECT_THROW(i.payload(), std::runtime_error);

    auto p = Int64Or<uint8_t>::forPayload(7);
    EXPECT_THROW(p.get(), std::runtime_error);
    EXPECT_THROW(p.getScaled(), std::runtime_error);
}

TEST(IntOr, Copy) {
    auto i = Int64Or<uint8_t>::forInt(-123);
    auto p = Int64Or<uint8_t>::forPayload(45);

    auto copy = i;
    EXPECT_TRUE(copy.isInt());
    EXPECT_EQ(copy.get(), -123);

    copy = p;
    EXPECT_FALSE(copy.isInt());
    EXPECT_EQ(copy.payload(), 45);

    static_assert(std::is_trivially_copyable_v<Int64Or<uint8_t>>);
    static_assert(sizeof(Int64Or<uint8_t>) == sizeof(int64_t));
    static_assert(sizeof(Int8Or<uint8_t>) == sizeof(int8_t));
}
