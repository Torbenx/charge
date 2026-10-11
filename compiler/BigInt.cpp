#include <BigInt.h>

#include <algorithm>
#include <compare>

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
        return limbView();
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

namespace {
using namespace bigint_detail;
using Magnitude = std::span<const Limb>;

//! Writes a + b into out[0, a.size()]
/*!
Requires a.size() >= b.size(). out may alias a or b.
*/
void addLimbs(Limb* out, Magnitude a, Magnitude b) {
    VERIFY(a.size() >= b.size());
    unsigned long long carry = 0;
    size_t i = 0;
    for (; i < b.size(); i++)
        out[i] = __builtin_addcll(a[i], b[i], carry, &carry);
    for (; i < a.size(); i++)
        out[i] = __builtin_addcll(a[i], 0, carry, &carry);
    out[a.size()] = carry;
}

//! Writes a - b into out[0, a.size())
/*!
Requires a >= b. out may alias a or b.
*/
void subLimbs(Limb* out, Magnitude a, Magnitude b) {
    VERIFY(a.size() >= b.size());
    unsigned long long borrow = 0;
    size_t i = 0;
    for (; i < b.size(); i++)
        out[i] = __builtin_subcll(a[i], b[i], borrow, &borrow);
    for (; i < a.size(); i++)
        out[i] = __builtin_subcll(a[i], 0, borrow, &borrow);
    VERIFY(borrow == 0);
}

//! Compares trimmed magnitudes
std::strong_ordering compareMagnitude(Magnitude a, Magnitude b) {
    if (a.size() != b.size())
        return a.size() <=> b.size();
    for (size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i])
            return a[i] <=> b[i];
    }
    return std::strong_ordering::equal;
}

//! Writes a * b into out[0, a.size()) and returns the high limb
Limb mulLimb(Limb* out, Magnitude a, Limb b) {
    Limb carry = 0;
    for (size_t i = 0; i < a.size(); i++) {
        __uint128_t product = static_cast<__uint128_t>(a[i]) * b + carry;
        out[i] = static_cast<Limb>(product);
        carry = static_cast<Limb>(product >> 64);
    }
    return carry;
}

//! Adds a * b to out[0, a.size()) and returns the high limb
Limb addMulLimb(Limb* out, Magnitude a, Limb b) {
    Limb carry = 0;
    for (size_t i = 0; i < a.size(); i++) {
        // At most (2^64 - 1)^2 + 2 * (2^64 - 1) = 2^128 - 1, so this cannot overflow
        __uint128_t product = static_cast<__uint128_t>(a[i]) * b + out[i] + carry;
        out[i] = static_cast<Limb>(product);
        carry = static_cast<Limb>(product >> 64);
    }
    return carry;
}

//! Number of limbs needed for the sum of numbers with the given limb counts and signs
uint32_t addCapacity(uint32_t aCount, bool aNegative, uint32_t bCount, bool bNegative) {
    return std::max(aCount, bCount) + (aNegative == bNegative ? 1 : 0);
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
    return BigInt::fromFields(Fields::makeAllocated(capacity));
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

bool BigInt::equalSlow(const BigInt& a, const BigInt& b) {
    return a.signedLimbCount() == b.signedLimbCount()
        && std::equal(a.limbData(), a.limbData() + a.limbCount(), b.limbData());
}

std::strong_ordering BigInt::compareSlow(const BigInt& a, const BigInt& b) {
    // Inline numbers lie strictly between the negative and positive allocated numbers, so ordering by
    // signed limb count with inline numbers counting as 0 decides all but equally long allocated numbers.
    int32_t aKey = a.storedInline() ? 0 : a.signedLimbCount();
    int32_t bKey = b.storedInline() ? 0 : b.signedLimbCount();
    if (aKey != bKey)
        return aKey <=> bKey;
    VERIFY(aKey != 0);
    auto magnitudeOrder = compareMagnitude(a.limbView(), b.limbView());
    return aKey < 0 ? (0 <=> magnitudeOrder) : magnitudeOrder;
}

BigInt BigInt::negateSlow(const BigInt& a) {
    BigInt result = a;
    result.negateInPlaceSlow();
    return result;
}

void BigInt::negateInPlaceSlow() {
    if (storedInline()) {
        // The only inline number whose negation does not fit inline
        VERIFY(inlineValue() == -inlineLimit);
        *this = allocateMagnitude128(inlineLimit, false);
        return;
    }
    // finish() moves the number inline if it was 2^126
    finish(!isNegative(), limbCount());
}

BigInt BigInt::addSlow(const BigInt& a, bool negateA, const BigInt& b, bool negateB) {
    std::array<Limb, 2> aScratch, bScratch;
    auto aMagnitude = a.magnitude(aScratch);
    auto bMagnitude = b.magnitude(bScratch);
    bool aNegative = a.isNegative() != negateA;
    bool bNegative = b.isNegative() != negateB;
    BigInt result = withCapacity(addCapacity(aMagnitude.size(), aNegative, bMagnitude.size(), bNegative));
    result.storeSignedAddition(aMagnitude, aNegative, bMagnitude, bNegative);
    return result;
}

void BigInt::addAssignSlow(bool negateThis, const BigInt& b, bool negateB) {
    if (storedInline()) {
        *this = addSlow(*this, negateThis, b, negateB);
        return;
    }
    bool aNegative = isNegative() != negateThis;
    bool bNegative = b.isNegative() != negateB;
    uint32_t bCount = b.storedInline() ? 2 : b.limbCount();
    reserve(addCapacity(limbCount(), aNegative, bCount, bNegative));
    // Take the magnitudes only after reserve(), which invalidates them if b is this number
    std::array<Limb, 2> bScratch;
    storeSignedAddition(limbView(), aNegative, b.magnitude(bScratch), bNegative);
}

void BigInt::storeSignedAddition(Magnitude a, bool aNegative, Magnitude b, bool bNegative) {
    Limb* out = limbData();
    if (aNegative == bNegative) {
        if (a.size() < b.size()) {
            std::swap(a, b);
            std::swap(aNegative, bNegative);
        }
        addLimbs(out, a, b);
        finish(aNegative, a.size() + 1);
    } else {
        if (compareMagnitude(a, b) < 0) {
            std::swap(a, b);
            std::swap(aNegative, bNegative);
        }
        subLimbs(out, a, b);
        finish(aNegative, a.size());
    }
}

BigInt BigInt::mulSlow(const BigInt& a, const BigInt& b) {
    __int128_t product;
    if (bothInline(a, b) && !__builtin_mul_overflow(a.scaledBits(), b.scaledBits() >> 1, &product))
        return fromFields(fromScaled(product));

    std::array<Limb, 2> aScratch, bScratch;
    auto aMagnitude = a.magnitude(aScratch);
    auto bMagnitude = b.magnitude(bScratch);
    if (aMagnitude.empty() || bMagnitude.empty())
        return BigInt();
    // Use the longer operand in the inner loop
    if (aMagnitude.size() > bMagnitude.size())
        std::swap(aMagnitude, bMagnitude);
    BigInt result = withCapacity(aMagnitude.size() + bMagnitude.size());
    Limb* out = result.limbData();
    out[bMagnitude.size()] = mulLimb(out, bMagnitude, aMagnitude[0]);
    for (size_t i = 1; i < aMagnitude.size(); i++)
        out[i + bMagnitude.size()] = addMulLimb(out + i, bMagnitude, aMagnitude[i]);
    result.finish(a.isNegative() != b.isNegative(), aMagnitude.size() + bMagnitude.size());
    return result;
}

void BigInt::mulAssignSlow(const BigInt& b) {
    *this = mulSlow(*this, b);
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

namespace {
BigInt big(bool negative, std::vector<Limb> magnitude) {
    return allocateAndFinish(negative, magnitude, magnitude.size());
}

// Covers the tiny and inline boundaries as well as allocated numbers which fit into __int128_t
const std::vector<__int128_t> int128Values = {
    0,
    1,
    -1,
    3,
    -3,
    pow2(62) - 1,
    pow2(62),
    -pow2(62),
    -pow2(62) - 1,
    pow2(63) - 1,
    pow2(63),
    -pow2(63),
    pow2(64) - 1,
    pow2(64) + 7,
    -pow2(64) - 7,
    pow2(100) + 12345,
    -pow2(100) - 12345,
    pow2(126) - 2,
    pow2(126) - 1,
    -pow2(126) + 1,
    -pow2(126),
    pow2(126),
    -pow2(126) - 1,
    pow2(126) + 1,
    std::numeric_limits<__int128_t>::max(),
    std::numeric_limits<__int128_t>::min(),
};

// Calls check(i, j, a, b) for all pairs of int128Values
template<typename F>
void forAllPairs(F check) {
    for (size_t i = 0; i < int128Values.size(); i++) {
        for (size_t j = 0; j < int128Values.size(); ++j) {
            SCOPED_TRACE(std::format("values {} and {}", int128Values[i], int128Values[j]));
            check(int128Values[i], int128Values[j]);
        }
    }
}

void expectEqual(const BigInt& actual, __int128_t expected) {
    // Representations are unique, so this also checks that the result is normalized
    EXPECT_TRUE(actual == BigInt::forInt128(expected));
}
}

TEST(BigInt, Equality) {
    EXPECT_TRUE(BigInt(5) == BigInt(5));
    EXPECT_TRUE(BigInt(5) == 5);
    EXPECT_FALSE(BigInt(5) == 6);
    EXPECT_FALSE(BigInt(-5) == 5);
    EXPECT_FALSE(BigInt::forInt128(pow2(126)) == BigInt::forInt128(pow2(126) - 1));
    EXPECT_FALSE(BigInt::forInt128(pow2(126) - 1) == BigInt::forInt128(pow2(126)));
    EXPECT_TRUE(BigInt::forInt128(-pow2(126)) == BigInt::forInt128(-pow2(126)));
    EXPECT_TRUE(allocateAndFinish(true, { 1, 2, 3 }, 3) == allocateAndFinish(true, { 1, 2, 3 }, 8));
    EXPECT_FALSE(big(true, { 1, 2, 3 }) == big(false, { 1, 2, 3 }));
    EXPECT_FALSE(big(true, { 1, 2, 3 }) == big(true, { 1, 2, 4 }));
    EXPECT_FALSE(big(true, { 1, 2, 3 }) == big(true, { 1, 2, 3, 4 }));
    EXPECT_TRUE(big(false, { 1, 2, 3 }) != big(false, { 1, 2 }));
}

TEST(BigInt, CompareInt128) {
    forAllPairs([](__int128_t a, __int128_t b) {
        BigInt x = BigInt::forInt128(a);
        BigInt y = BigInt::forInt128(b);
        EXPECT_EQ(x <=> y, a <=> b);
        EXPECT_EQ(x < y, a < b);
        EXPECT_EQ(x <= y, a <= b);
        EXPECT_EQ(x > y, a > b);
        EXPECT_EQ(x >= y, a >= b);
    });
}

TEST(BigInt, CompareMultiLimb) {
    // Allocated magnitudes in ascending order, neighbors with the same length differ in a single limb
    std::vector<std::vector<Limb>> magnitudes = {
        { 0, 1ull << 62 },
        { ~0ull, ~0ull },
        { 1, 2, 3 },
        { 2, 2, 3 },
        { 0, 3, 3 },
        { 0, 0, 4 },
        { ~0ull, ~0ull, ~0ull },
        { 0, 0, 0, 1 },
    };
    // Ascending values: negative allocated numbers, inline numbers and positive allocated numbers
    std::vector<BigInt> values;
    for (size_t i = magnitudes.size(); i-- > 0;)
        values.push_back(big(true, magnitudes[i]));
    for (__int128_t value : { -pow2(126) + 1, -pow2(64), static_cast<__int128_t>(-1), static_cast<__int128_t>(0),
             static_cast<__int128_t>(1), pow2(64), pow2(126) - 1 })
        values.push_back(BigInt::forInt128(value));
    for (const auto& magnitude : magnitudes)
        values.push_back(big(false, magnitude));

    for (size_t i = 0; i < values.size(); i++) {
        for (size_t j = 0; j < values.size(); ++j) {
            // TODO: Print numeric values once BigInt formatting is implemented.
            SCOPED_TRACE(std::format("value numbers {} and {}", i, j));
            EXPECT_EQ(values[i] <=> values[j], i <=> j);
        }
    }
}

TEST(BigInt, CompareEqualAllocated) {
    // Equal numbers with different capacities compare equal
    EXPECT_EQ(allocateAndFinish(false, { 1, 2, 3 }, 3) <=> allocateAndFinish(false, { 1, 2, 3 }, 8),
        std::strong_ordering::equal);
    EXPECT_EQ(allocateAndFinish(true, { 1, 2, 3 }, 8) <=> allocateAndFinish(true, { 1, 2, 3 }, 3),
        std::strong_ordering::equal);
    BigInt a = big(true, { 1, 2, 3 });
    EXPECT_EQ(a <=> a, std::strong_ordering::equal);
}

TEST(BigInt, CompareBuiltinIntegers) {
    EXPECT_TRUE(BigInt(5) < 6);
    EXPECT_TRUE(6 > BigInt(5));
    EXPECT_TRUE(BigInt(-1) < 0);
    EXPECT_TRUE(0 <= BigInt(0));
    EXPECT_TRUE(BigInt(std::numeric_limits<int64_t>::min()) < std::numeric_limits<int64_t>::max());
    EXPECT_TRUE(BigInt(std::numeric_limits<uint64_t>::max()) > std::numeric_limits<int64_t>::max());
    EXPECT_TRUE(big(false, { 1, 2, 3 }) > 0);
    EXPECT_TRUE(0 > big(true, { 1, 2, 3 }));
    EXPECT_TRUE(std::numeric_limits<int64_t>::min() > big(true, { 1, 2, 3 }));
    EXPECT_TRUE(std::numeric_limits<uint64_t>::max() < big(false, { 1, 2, 3 }));
}

TEST(BigInt, Negate) {
    for (__int128_t value : int128Values) {
        if (value == std::numeric_limits<__int128_t>::min())
            continue;
        SCOPED_TRACE(std::format("value {}", (int64_t)value));
        BigInt a = BigInt::forInt128(value);
        expectEqual(-a, -value);
        expectEqual(-BigInt(a), -value);
        BigInt b = a;
        b.negateInPlace();
        expectEqual(b, -value);
    }

    expectAllocated(-BigInt::forInt128(-pow2(126)), false, { 0, 1ull << 62 });
    expectInline(-BigInt::forInt128(pow2(126)), -pow2(126));
    expectAllocated(-BigInt::forInt128(std::numeric_limits<__int128_t>::min()), false, { 0, 1ull << 63 });

    BigInt a = BigInt::forInt128(-pow2(126));
    a.negateInPlace();
    expectAllocated(a, false, { 0, 1ull << 62 });
    a.negateInPlace();
    expectInline(a, -pow2(126));

    // Negating in place keeps the allocation
    BigInt c = allocateAndFinish(true, { 1, 2, 3 }, 8);
    Limb* cLimbs = f(c).limbData();
    c.negateInPlace();
    expectAllocated(c, false, { 1, 2, 3 });
    EXPECT_EQ(f(c).limbData(), cLimbs);
    BigInt d = -std::move(c);
    expectAllocated(d, true, { 1, 2, 3 });
    EXPECT_EQ(f(d).limbData(), cLimbs);
}

TEST(BigInt, AddInt128) {
    forAllPairs([](__int128_t a, __int128_t b) {
        __int128_t expected;
        if (__builtin_add_overflow(a, b, &expected))
            return;
        BigInt x = BigInt::forInt128(a);
        BigInt y = BigInt::forInt128(b);
        expectEqual(x + y, expected);
        expectEqual(BigInt(x) + y, expected);
        expectEqual(x + BigInt(y), expected);
        expectEqual(BigInt(x) + BigInt(y), expected);
        BigInt z = x;
        z += y;
        expectEqual(z, expected);
    });
}

TEST(BigInt, SubInt128) {
    forAllPairs([](__int128_t a, __int128_t b) {
        __int128_t expected;
        if (__builtin_sub_overflow(a, b, &expected))
            return;
        BigInt x = BigInt::forInt128(a);
        BigInt y = BigInt::forInt128(b);
        expectEqual(x - y, expected);
        expectEqual(BigInt(x) - y, expected);
        expectEqual(x - BigInt(y), expected);
        expectEqual(BigInt(x) - BigInt(y), expected);
        BigInt z = x;
        z -= y;
        expectEqual(z, expected);
    });
}

TEST(BigInt, MulInt128) {
    forAllPairs([](__int128_t a, __int128_t b) {
        __int128_t expected;
        if (__builtin_mul_overflow(a, b, &expected))
            return;
        BigInt x = BigInt::forInt128(a);
        BigInt y = BigInt::forInt128(b);
        expectEqual(x * y, expected);
        BigInt z = x;
        z *= y;
        expectEqual(z, expected);
    });
}

TEST(BigInt, CarryAndBorrow) {
    BigInt allOnes = big(false, { ~0ull, ~0ull, ~0ull });
    BigInt power = big(false, { 0, 0, 0, 1 });
    EXPECT_TRUE(allOnes + 1 == power);
    EXPECT_TRUE(power - 1 == allOnes);
    EXPECT_TRUE(1 - power == -allOnes);
    EXPECT_TRUE(-1 - allOnes == -power);
    EXPECT_TRUE(-allOnes - 1 == -power);
    expectInline(power - allOnes, 1);
    expectInline(-power + allOnes, -1);
    // Cancellation of the high limbs
    expectInline(big(false, { 5, 7, 9 }) - big(false, { 2, 7, 9 }), 3);
    expectInline(big(false, { 5, 7, 9 }) - big(false, { 2, 6, 9 }), pow2(64) + 3);
    expectAllocated(big(false, { 5, 7, 9 }) - big(false, { 2, 7, 8 }), false, { 3, 0, 1 });
}

TEST(BigInt, MulMultiLimb) {
    BigInt max128 = BigInt::forUInt128(std::numeric_limits<__uint128_t>::max());
    // (2^128 - 1)^2 = 2^256 - 2^129 + 1
    expectAllocated(max128 * max128, false, { 1, 0, ~0ull - 1, ~0ull });
    expectAllocated(max128 * -max128, true, { 1, 0, ~0ull - 1, ~0ull });
    expectAllocated(big(true, { 0, 0, 1 }) * big(true, { 0, 0, 0, 1 }), false, { 0, 0, 0, 0, 0, 1 });
    // (2^64 - 1) * (2^192 - 1) = 2^256 - 2^192 - 2^64 + 1
    expectAllocated(BigInt(~0ull) * big(false, { ~0ull, ~0ull, ~0ull }), false, { 1, ~0ull, ~0ull, ~0ull - 1 });
    expectInline(big(false, { 1, 2, 3 }) * 0, 0);
    expectInline(BigInt(0) * big(true, { 1, 2, 3 }), 0);
    // Inline operands whose product overflows the inline range
    expectAllocated(BigInt::forInt128(pow2(126) - 1) * BigInt::forInt128(-pow2(126)), true, { 0, 3ull << 62, ~0ull, (1ull << 60) - 1 });
}

TEST(BigInt, Identities) {
    std::vector<BigInt> values = {
        0,
        1,
        -1,
        -12345,
        BigInt::forInt128(pow2(62)),
        BigInt::forInt128(-pow2(100) - 3),
        BigInt::forInt128(-pow2(126)),
        BigInt::forInt128(pow2(126)),
        big(false, { 0x0123456789abcdef, 0xfedcba9876543210, 0x0f0f0f0f0f0f0f0f, 7 }),
        big(true, { ~0ull, 0, ~0ull, 0x8000000000000000, 1 }),
        big(false, { ~0ull, ~0ull, ~0ull }),
    };
    for (size_t i = 0; i < values.size(); i++) {
        const BigInt& a = values[i];
        EXPECT_TRUE(-(-a) == a);
        EXPECT_TRUE(a - a == 0);
        EXPECT_TRUE(a + -a == 0);
        EXPECT_TRUE(a * -1 == -a);
        EXPECT_TRUE(a * 1 == a);
        EXPECT_TRUE(a * 0 == 0);
        for (size_t j = 0; j < values.size(); ++j) {
            const BigInt& b = values[j];
            EXPECT_TRUE(a + b == b + a);
            EXPECT_TRUE((a + b) - b == a);
            EXPECT_TRUE((a - b) + b == a);
            EXPECT_TRUE(a - b == -(b - a));
            EXPECT_TRUE(a * b == b * a);
            EXPECT_TRUE(-a * b == -(a * b));
            for (size_t k = 0; k < values.size(); ++k) {
                // TODO: Print numeric values once BigInt formatting is implemented.
                SCOPED_TRACE(std::format("value numbers {}, {} and {}", i, j, k));
                const BigInt& c = values[k];
                EXPECT_TRUE((a + b) * c == a * c + b * c);
                EXPECT_TRUE((a - b) * c == a * c - b * c);
                EXPECT_TRUE((a * b) * c == a * (b * c));
                EXPECT_TRUE((a + b) + c == a + (b + c));
            }
        }
    }
}

TEST(BigInt, InPlaceReusesAllocation) {
    BigInt a = allocateAndFinish(false, { 1, 2, 3 }, 8);
    Limb* aLimbs = f(a).limbData();
    a += 5;
    expectAllocated(a, false, { 6, 2, 3 });
    a -= big(false, { 0, 0, 1 });
    expectAllocated(a, false, { 6, 2, 2 });
    a += a;
    expectAllocated(a, false, { 12, 4, 4 });
    a -= big(false, { 0, 0, 0, 1 });
    expectAllocated(a, true, { ~0ull - 11, ~0ull - 4, ~0ull - 4 });
    EXPECT_EQ(f(a).limbData(), aLimbs);
    EXPECT_EQ(f(a).capacity(), 8u);

    // Growing the allocation
    BigInt b = big(false, { ~0ull, ~0ull, ~0ull });
    b += 1;
    expectAllocated(b, false, { 0, 0, 0, 1 });
    b -= 1;
    expectAllocated(b, false, { ~0ull, ~0ull, ~0ull });
    b += b;
    expectAllocated(b, false, { ~0ull - 1, ~0ull, ~0ull, 1 });

    // Shrinking back inline
    BigInt c = big(true, { 1, 2, 3 });
    c -= big(true, { 0, 2, 3 });
    expectInline(c, -1);
}

TEST(BigInt, RValueOperandsReuseAllocation) {
    BigInt a = allocateAndFinish(false, { 1, 2, 3 }, 8);
    Limb* aLimbs = f(a).limbData();
    BigInt b = std::move(a) + 1;
    expectAllocated(b, false, { 2, 2, 3 });
    EXPECT_EQ(f(b).limbData(), aLimbs);
    BigInt c = 1 + std::move(b);
    expectAllocated(c, false, { 3, 2, 3 });
    EXPECT_EQ(f(c).limbData(), aLimbs);
    BigInt d = std::move(c) - 1;
    expectAllocated(d, false, { 2, 2, 3 });
    EXPECT_EQ(f(d).limbData(), aLimbs);
    BigInt e = 1 - std::move(d);
    expectAllocated(e, true, { 1, 2, 3 });
    EXPECT_EQ(f(e).limbData(), aLimbs);
    BigInt g = std::move(e) + BigInt(-1);
    expectAllocated(g, true, { 2, 2, 3 });
    EXPECT_EQ(f(g).limbData(), aLimbs);
    BigInt h = std::move(g) - BigInt(-1);
    expectAllocated(h, true, { 1, 2, 3 });
    EXPECT_EQ(f(h).limbData(), aLimbs);
    BigInt k = BigInt(1) + std::move(h);
    expectAllocated(k, true, { 0, 2, 3 });
    EXPECT_EQ(f(k).limbData(), aLimbs);
    BigInt m = BigInt(1) - std::move(k);
    expectAllocated(m, false, { 1, 2, 3 });
    EXPECT_EQ(f(m).limbData(), aLimbs);
}

TEST(BigInt, Aliasing) {
    for (__int128_t value : { (__int128_t)7, -pow2(62), pow2(126) - 1, -pow2(126) - 5 }) {
        SCOPED_TRACE(std::format("value {}", (int64_t)value));
        const BigInt original = BigInt::forInt128(value);
        BigInt a = original;
        a += a;
        EXPECT_TRUE(a == original * 2);
        a = original;
        a -= a;
        expectInline(a, 0);
        a = original;
        a *= a;
        EXPECT_TRUE(a == original * original);
        a = original;
        EXPECT_TRUE(a - std::move(a) == 0);
        a = original;
        EXPECT_TRUE(std::move(a) - a == 0);
        a = original;
        EXPECT_TRUE(a + std::move(a) == original * 2);
    }
}
