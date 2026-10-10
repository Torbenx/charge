#include <BigInt.h>

#include <algorithm>

#include <gtest/gtest.h>

namespace bigint_detail {

Fields Fields::makeAllocated(uint32_t capacity) {
    VERIFY(capacity > 0);
    VERIFY(capacity <= maxLimbs);
    Limb* limbs = std::allocator<Limb>().allocate(capacity);
    // tiny() relies on the highest bit of the pointer being 0.
    VERIFY(std::bit_cast<uintptr_t>(limbs) >> 63 == 0);
    return { UInt64Or<Limb*>::forPayload(limbs), packSize(0, capacity) };
}

Fields Fields::makeFittingCopy(const Fields& other) {
    VERIFY(!other.storedInline());
    uint32_t limbCount = other.limbCount();
    Fields result = makeAllocated(limbCount);
    std::copy_n(other.limbData(), limbCount, result.limbData());
    result.setSignedLimbCount(other.signedLimbCount());
    return result;
}

void Fields::deallocateLimbs() {
    std::allocator<Limb>().deallocate(limbData(), capacity());
}

std::span<const Limb> Fields::magnitude(std::array<Limb, 2>& scratch) const {
    if (!storedInline())
        return { limbData(), limbCount() };
    __int128_t value = inlineValue();
    auto magnitude = static_cast<__uint128_t>(value);
    if (value < 0)
        magnitude = -magnitude;
    scratch[0] = static_cast<Limb>(magnitude);
    scratch[1] = static_cast<Limb>(magnitude >> 64);
    uint32_t count = scratch[1] != 0 ? 2 : (scratch[0] != 0 ? 1 : 0);
    return { scratch.data(), count };
}

}

BigInt BigInt::forInt128(__int128_t value) {
    if (fitsInlineInt128(value)) {
        BigInt result;
        result.setInlineUnchecked(value);
        return result;
    }
    bool negative = value < 0;
    auto magnitude = static_cast<__uint128_t>(value);
    if (negative)
        magnitude = -magnitude;
    return allocateMagnitude128(magnitude, negative);
}

BigInt BigInt::forUInt128(__uint128_t value) {
    if (fitsInlineUInt128(value)) {
        BigInt result;
        result.setInlineUnchecked(value);
        return result;
    }
    return allocateMagnitude128(value, false);
}

BigInt BigInt::allocateMagnitude128(__uint128_t magnitude, bool negative) {
    BigInt result = withCapacity(2);
    Limb* limbs = result.limbData();
    limbs[0] = static_cast<Limb>(magnitude);
    limbs[1] = static_cast<Limb>(magnitude >> 64);
    result.setLimbCountAndSign(2, negative);
    return result;
}

BigInt& BigInt::operator=(const BigInt& other) {
    if (this == &other) [[unlikely]]
        return *this;
    if (other.storedInline()) {
        release();
        setFields(other);
    } else if (!storedInline() && capacity() >= other.limbCount()) {
        // Reuse the existing allocation
        std::copy_n(other.limbData(), other.limbCount(), limbData());
        setSignedLimbCount(other.signedLimbCount());
    } else {
        Fields copy = makeFittingCopy(other);
        release();
        setFields(copy);
    }
    return *this;
}

BigInt BigInt::withCapacity(uint32_t capacity) {
    BigInt result;
    result.setFields(Fields::makeAllocated(capacity));
    return result;
}

void BigInt::finish(bool negative, uint32_t count) {
    VERIFY(count <= capacity());
    Limb* limbs = limbData();
    while (count > 0 && limbs[count - 1] == 0)
        --count;
    if (count <= 2) {
        __uint128_t magnitude = 0;
        if (count >= 1)
            magnitude |= limbs[0];
        if (count == 2)
            magnitude |= static_cast<__uint128_t>(limbs[1]) << 64;
        auto limit = static_cast<__uint128_t>(inlineLimit);
        if (magnitude < limit || (negative && magnitude == limit)) {
            release();
            auto value = static_cast<__int128_t>(magnitude);
            setInlineUnchecked(negative ? -value : value);
            return;
        }
    }
    setLimbCountAndSign(count, negative);
}

void BigInt::reserve(uint32_t minCapacity) {
    uint32_t oldCapacity = capacity();
    if (oldCapacity >= minCapacity)
        return;
    // Enforce a 1.5x growth factor
    uint32_t newCapacity = std::max(minCapacity, std::min(maxLimbs, oldCapacity + oldCapacity / 2));
    Fields newFields = Fields::makeAllocated(newCapacity);
    std::copy_n(limbData(), limbCount(), newFields.limbData());
    newFields.setSignedLimbCount(signedLimbCount());
    std::swap((Fields&)*this, newFields);
    newFields.deallocateLimbs();
}

struct BigIntTestAccess {
    using Limb = BigInt::Limb;

    static bigint_detail::Fields& fields(BigInt& v) { return (bigint_detail::Fields&)v; }
    static const bigint_detail::Fields& fields(const BigInt& v) { return (const bigint_detail::Fields&)v; }
    static void finish(BigInt& v, bool negative, uint32_t count) { v.finish(negative, count); }
    static void reserve(BigInt& v, uint32_t minCapacity) { v.reserve(minCapacity); }
};

namespace {
using namespace bigint_detail;
using A = BigIntTestAccess;
Fields& f(BigInt& v) { return A::fields(v); }
const Fields& f(const BigInt& v) { return A::fields(v); }

constexpr __int128_t pow2(int n) {
    return static_cast<__int128_t>(1) << n;
}

void expectInline(const BigInt& v, __int128_t expected) {
    ASSERT_TRUE(f(v).storedInline());
    EXPECT_TRUE(f(v).inlineValue() == expected);
    EXPECT_EQ(v.isNegative(), expected < 0);
    EXPECT_EQ(f(v).tiny(), expected >= -pow2(62) && expected < pow2(62));
}

void expectAllocated(const BigInt& v, bool negative, std::vector<Limb> magnitude) {
    ASSERT_FALSE(f(v).storedInline());
    EXPECT_FALSE(f(v).tiny());
    EXPECT_EQ(v.isNegative(), negative);
    ASSERT_EQ(f(v).limbCount(), magnitude.size());
    EXPECT_GE(f(v).capacity(), f(v).limbCount());
    EXPECT_TRUE(std::equal(magnitude.begin(), magnitude.end(), f(v).limbData()));
}

BigInt allocateAndFinish(bool negative, std::vector<Limb> magnitude, uint32_t capacity) {
    BigInt result;
    f(result) = Fields::makeAllocated(capacity);
    std::copy(magnitude.begin(), magnitude.end(), f(result).limbData());
    A::finish(result, negative, magnitude.size());
    return result;
}
}

TEST(BigInt, BuiltinIntegers) {
    expectInline(BigInt(), 0);
    expectInline(BigInt(0), 0);
    expectInline(BigInt(1), 1);
    expectInline(BigInt(-1), -1);
    expectInline(BigInt(std::numeric_limits<int8_t>::min()), -128);
    expectInline(BigInt(std::numeric_limits<int64_t>::min()), std::numeric_limits<int64_t>::min());
    expectInline(BigInt(std::numeric_limits<int64_t>::max()), std::numeric_limits<int64_t>::max());
    expectInline(BigInt(std::numeric_limits<uint64_t>::max()), std::numeric_limits<uint64_t>::max());
}

TEST(BigInt, TinyBoundaries) {
    expectInline(BigInt(static_cast<int64_t>(-pow2(62))), -pow2(62));
    expectInline(BigInt(static_cast<int64_t>(pow2(62) - 1)), pow2(62) - 1);
    expectInline(BigInt(static_cast<int64_t>(pow2(62))), pow2(62));
    expectInline(BigInt(static_cast<int64_t>(-pow2(62) - 1)), -pow2(62) - 1);
}

TEST(BigInt, FromInt128) {
    expectInline(BigInt::forInt128(-pow2(126)), -pow2(126));
    expectInline(BigInt::forInt128(pow2(126) - 1), pow2(126) - 1);
    expectInline(BigInt::forInt128(-pow2(100) - 12345), -pow2(100) - 12345);
    expectAllocated(BigInt::forInt128(pow2(126)), false, { 0, 1ull << 62 });
    expectAllocated(BigInt::forInt128(-pow2(126) - 1), true, { 1, 1ull << 62 });
    expectAllocated(BigInt::forInt128(std::numeric_limits<__int128_t>::min()), true, { 0, 1ull << 63 });
    expectAllocated(BigInt::forInt128(std::numeric_limits<__int128_t>::max()), false, { ~0ull, ~0ull >> 1 });
}

TEST(BigInt, FromUInt128) {
    expectInline(BigInt::forUInt128(0), 0);
    expectInline(BigInt::forUInt128(pow2(126) - 1), pow2(126) - 1);
    expectAllocated(BigInt::forUInt128(pow2(126)), false, { 0, 1ull << 62 });
    expectAllocated(BigInt::forUInt128(static_cast<__uint128_t>(1) << 127), false, { 0, 1ull << 63 });
    expectAllocated(BigInt::forUInt128(std::numeric_limits<__uint128_t>::max()), false, { ~0ull, ~0ull });
}

TEST(BigInt, TinyAllocated) {
    // The size field of negative numbers has the highest bit set, unlike the pointer
    EXPECT_FALSE(f(BigInt::forInt128(-pow2(126) - 1)).tiny());
    EXPECT_FALSE(f(BigInt::forInt128(std::numeric_limits<__int128_t>::min())).tiny());
    EXPECT_FALSE(f(allocateAndFinish(true, { 1, 2, 3 }, 3)).tiny());
    EXPECT_FALSE(f(allocateAndFinish(true, { 1, 2, 3 }, 8)).tiny());
}

TEST(BigInt, Finish) {
    expectInline(allocateAndFinish(false, { 0, 0, 0 }, 3), 0);
    expectInline(allocateAndFinish(true, { 0, 0, 0 }, 3), 0);
    expectInline(allocateAndFinish(true, { 5, 0, 0, 0 }, 4), -5);
    expectInline(allocateAndFinish(false, { ~0ull, (1ull << 62) - 1 }, 2), pow2(126) - 1);
    expectInline(allocateAndFinish(true, { 0, 1ull << 62, 0 }, 3), -pow2(126));
    expectAllocated(allocateAndFinish(false, { 0, 1ull << 62, 0 }, 3), false, { 0, 1ull << 62 });
    expectAllocated(allocateAndFinish(true, { 1, 2, 3, 0 }, 4), true, { 1, 2, 3 });
    EXPECT_EQ(f(allocateAndFinish(true, { 1, 2, 3, 0 }, 8)).capacity(), 8u);
}

TEST(BigInt, Magnitude) {
    std::array<Limb, 2> scratch;
    auto view = f(BigInt()).magnitude(scratch);
    EXPECT_EQ(view.size(), 0u);

    view = f(BigInt(-7)).magnitude(scratch);
    EXPECT_EQ(view.data(), scratch.data());
    EXPECT_EQ(view.size(), 1u);
    EXPECT_EQ(view[0], 7u);

    view = f(BigInt::forInt128(-pow2(126))).magnitude(scratch);
    EXPECT_EQ(view.size(), 2u);
    EXPECT_EQ(view[0], 0u);
    EXPECT_EQ(view[1], 1ull << 62);

    BigInt allocated = allocateAndFinish(true, { 1, 2, 3 }, 4);
    view = f(allocated).magnitude(scratch);
    EXPECT_EQ(view.data(), f(allocated).limbData());
    EXPECT_EQ(view.size(), 3u);
}

TEST(BigInt, Copy) {
    BigInt a = allocateAndFinish(true, { 1, 2, 3 }, 8);
    BigInt b = a;
    expectAllocated(b, true, { 1, 2, 3 });
    EXPECT_NE(f(a).limbData(), f(b).limbData());
    EXPECT_EQ(f(b).capacity(), 3u);

    BigInt c = BigInt(42);
    BigInt d = c;
    expectInline(d, 42);
}

TEST(BigInt, CopyAssign) {
    BigInt a = allocateAndFinish(false, { 1, 2, 3, 4 }, 4);
    Limb* aLimbs = f(a).limbData();

    // Reuse the allocation if it is big enough
    BigInt small = allocateAndFinish(true, { 5, 6, 7 }, 3);
    a = small;
    expectAllocated(a, true, { 5, 6, 7 });
    EXPECT_EQ(f(a).limbData(), aLimbs);
    EXPECT_EQ(f(a).capacity(), 4u);

    // Allocate if it is too small
    BigInt big = allocateAndFinish(false, { 1, 2, 3, 4, 5 }, 5);
    a = big;
    expectAllocated(a, false, { 1, 2, 3, 4, 5 });
    EXPECT_NE(f(a).limbData(), f(big).limbData());

    // Allocate if stored inline
    BigInt b = 7;
    b = big;
    expectAllocated(b, false, { 1, 2, 3, 4, 5 });

    a = BigInt(-3);
    expectInline(a, -3);

    BigInt& aRef = a;
    a = aRef;
    expectInline(a, -3);
    BigInt& bRef = b;
    b = bRef;
    expectAllocated(b, false, { 1, 2, 3, 4, 5 });
}

TEST(BigInt, Move) {
    BigInt a = allocateAndFinish(true, { 1, 2, 3 }, 4);
    Limb* aLimbs = f(a).limbData();

    BigInt b = std::move(a);
    expectInline(a, 0);
    expectAllocated(b, true, { 1, 2, 3 });
    EXPECT_EQ(f(b).limbData(), aLimbs);

    BigInt c = allocateAndFinish(false, { 4, 5, 6 }, 3);
    c = std::move(b);
    expectInline(b, 0);
    expectAllocated(c, true, { 1, 2, 3 });
    EXPECT_EQ(f(c).limbData(), aLimbs);


    BigInt& cRef = c;
    c = std::move(cRef);
    expectInline(c, 0);

    c = BigInt(9);
    expectInline(c, 9);
}

TEST(BigInt, Reserve) {
    BigInt a = allocateAndFinish(false, { 1, 2, 3 }, 3);
    A::reserve(a, 2);
    EXPECT_EQ(f(a).capacity(), 3u);
    A::reserve(a, 4);
    EXPECT_GE(f(a).capacity(), 4u);
    expectAllocated(a, false, { 1, 2, 3 });
    A::reserve(a, 100);
    EXPECT_EQ(f(a).capacity(), 100u);
    expectAllocated(a, false, { 1, 2, 3 });
}
