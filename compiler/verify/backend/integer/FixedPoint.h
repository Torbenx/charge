#pragma once

#include <BigInt.h>
#include <log.h>
#include <types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <compare>
#include <concepts>
#include <format>
#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>

namespace verify::backend::integer {

//! Layout of a fixed-point number in 32-bit words
/*!
A number is stored as a two's complement integer of totalWords() words in little endian order, which is
scaled by 2^(-32 * fractionWords). The integer part includes the sign bit, so the representable range is
[-2^(32 * integerWords - 1), 2^(32 * integerWords - 1)) with a resolution of 2^(-32 * fractionWords).
*/
struct FixedPointLayout {
    uint32_t integerWords;
    uint32_t fractionWords;

    constexpr uint32_t totalWords() const { return integerWords + fractionWords; }

    //! Returns whether every number of the other layout is exactly representable in this layout
    constexpr bool contains(FixedPointLayout other) const {
        return integerWords >= other.integerWords && fractionWords >= other.fractionWords;
    }

    friend constexpr bool operator==(FixedPointLayout, FixedPointLayout) = default;
};

constexpr bool validLayout(FixedPointLayout layout) {
    return layout.integerWords >= 1;
}

//! Layout which holds the exact product of numbers with layouts a and b
/*!
The magnitude of a product is at most 2^(32 * integerWords - 2) for the integer words of this layout.
*/
constexpr FixedPointLayout productLayout(FixedPointLayout a, FixedPointLayout b) {
    return { a.integerWords + b.integerWords, a.fractionWords + b.fractionWords };
}

//! Layout which holds the exact sum of up to 2^32 products of numbers with layouts a and b
/*!
One guard word suffices because of the magnitude bound on products. Sparse vectors and matrices use 32-bit
indices, so their dot products never have more terms.
*/
constexpr FixedPointLayout dotLayout(FixedPointLayout a, FixedPointLayout b) {
    FixedPointLayout product = productLayout(a, b);
    return { product.integerWords + 1, product.fractionWords };
}

//! Layout of the integers which holds every value of the integer type
template<std::integral T>
constexpr FixedPointLayout integerLayoutFor() {
    // The digits exclude the sign bit
    return { static_cast<uint32_t>((std::numeric_limits<T>::digits + 32) / 32), 0 };
}

enum class RoundingMode : uint8_t {
    Floor,
    Ceil,
    TowardZero,
    //! Round to nearest, ties to even
    Nearest,
};

template<FixedPointLayout L>
    requires(validLayout(L))
class FixedPoint;

}

namespace verify::backend::integer::fixed_point_detail {

template<int_t N>
using Words = std::array<uint32_t, N>;

template<int_t N>
using Limbs = std::array<uint64_t, N>;

constexpr uint64_t signLimb(uint64_t limb) {
    return static_cast<uint64_t>(static_cast<int64_t>(limb) >> 63);
}

inline uint64_t addCarry(uint64_t a, uint64_t b, uint64_t& carry) {
    unsigned long long carryOut;
    uint64_t result = __builtin_addcll(a, b, carry, &carryOut);
    carry = carryOut;
    return result;
}

inline uint64_t subBorrow(uint64_t a, uint64_t b, uint64_t& borrow) {
    unsigned long long borrowOut;
    uint64_t result = __builtin_subcll(a, b, borrow, &borrowOut);
    borrow = borrowOut;
    return result;
}

//! Returns the limb at the given index of the mask 2^bits - 1
constexpr uint64_t lowBitsMask(int_t index, int_t bits) {
    if (bits >= 64 * (index + 1))
        return ~uint64_t(0);
    if (bits <= 64 * index)
        return 0;
    return (uint64_t(1) << (bits - 64 * index)) - 1;
}

//! Returns the signed top sign extended to a limb
template<FixedPointLayout L>
constexpr uint64_t topLimb(const FixedPoint<L>& x) {
    return static_cast<uint64_t>(static_cast<int64_t>(x.getTop()));
}

//! Returns the limb at the given index of the infinitely sign extended number
/*!
The limbs below the lowest one are zero and the limb at index limbCount is the sign extended top.
*/
template<FixedPointLayout L>
constexpr uint64_t extendedLimb(const FixedPoint<L>& x, int_t index) {
    constexpr auto limbCount = FixedPoint<L>::limbCount;
    if (index < 0)
        return 0;
    if (index < limbCount)
        return x.getLimb(index);
    return index == limbCount ? topLimb(x) : signLimb(topLimb(x));
}

//! Returns the 64 bits of the infinitely sign extended number which start at the given word index
template<FixedPointLayout L>
constexpr uint64_t wordsAt(const FixedPoint<L>& x, int_t word) {
    // The arithmetic shift rounds down for negative words
    int_t index = word >> 1;
    if (word % 2 == 0)
        return extendedLimb(x, index);
    return (extendedLimb(x, index) >> 32) | (extendedLimb(x, index + 1) << 32);
}

//! Stores in scaled by 2^(32 * shift) into out and returns whether the result overflowed
/*!
Words shifted below the lowest word are truncated, so the result is rounded down.
*/
template<int_t shift, FixedPointLayout From, FixedPointLayout To>
constexpr bool shiftOverflow(const FixedPoint<From>& in, FixedPoint<To>& out) {
    using Out = FixedPoint<To>;
    Out result;
    for (int_t i = 0; i < Out::limbCount; i++)
        result.setLimb(i, wordsAt(in, 2 * i - shift));
    result.setTop(static_cast<typename Out::Top>(wordsAt(in, 2 * Out::limbCount - shift)));
    // The words of the input above the result have to be the sign extension of the result
    uint64_t sign = signLimb(topLimb(result));
    bool overflow = false;
    for (int_t word = int_t(To.totalWords()) - shift; word < int_t(From.totalWords()); word += 2)
        overflow |= wordsAt(in, word) != sign;
    out = result;
    return overflow;
}

//! Adds x to acc with sign extension and wraps around on overflow
template<FixedPointLayout D, FixedPointLayout L>
    requires(D.contains(L) && D.fractionWords == L.fractionWords)
void addSignExtended(FixedPoint<D>& acc, const FixedPoint<L>& x) {
    constexpr auto limbCount = FixedPoint<D>::limbCount;
    uint64_t carry = 0;
    for (int_t i = 0; i < limbCount; i++)
        acc.setLimb(i, addCarry(acc.getLimb(i), extendedLimb(x, i), carry));
    acc.setTop(static_cast<typename FixedPoint<D>::Top>(topLimb(acc) + extendedLimb(x, limbCount) + carry));
}

//! Subtracts x from acc with sign extension and wraps around on overflow
template<FixedPointLayout D, FixedPointLayout L>
    requires(D.contains(L) && D.fractionWords == L.fractionWords)
void subSignExtended(FixedPoint<D>& acc, const FixedPoint<L>& x) {
    constexpr auto limbCount = FixedPoint<D>::limbCount;
    uint64_t borrow = 0;
    for (int_t i = 0; i < limbCount; i++)
        acc.setLimb(i, subBorrow(acc.getLimb(i), extendedLimb(x, i), borrow));
    acc.setTop(static_cast<typename FixedPoint<D>::Top>(topLimb(acc) - extendedLimb(x, limbCount) - borrow));
}

//! Returns -x and wraps around on overflow
template<FixedPointLayout L>
FixedPoint<L> negateWrapping(const FixedPoint<L>& x) {
    FixedPoint<L> result;
    subSignExtended(result, x);
    return result;
}

//! Returns the magnitude of x, which is exact because the wide layout has an additional integer word
template<FixedPointLayout Wide, FixedPointLayout L>
    requires(Wide.integerWords > L.integerWords && Wide.fractionWords >= L.fractionWords)
FixedPoint<Wide> magnitude(const FixedPoint<L>& x) {
    FixedPoint<Wide> result = x.template widen<Wide>();
    return x.isNegative() ? negateWrapping(result) : result;
}

}

namespace verify::backend::integer {

//! Signed fixed-point number with a layout fixed at compile time
/*!
The storage consists only of 32-bit words, so the alignment is 4 and a value with layout {2, 1} can share
16 bytes with a 32-bit index in a sparse matrix entry.

Computations access the words as unsigned 64-bit limbs through getLimb() and setLimb(), except for the signed
top. It consists of the one or two words above the limbs and is accessed through getTop() and setTop().

Operations which can overflow return whether they overflowed, in which case the result is wrapped
around. Operations returning a wider layout are exact and never overflow.
*/
template<FixedPointLayout L>
    requires(validLayout(L))
class FixedPoint {
public:
    static constexpr FixedPointLayout layout = L;
    static constexpr int_t wordCount = L.totalWords();
    //! Number of unsigned limbs below the signed top
    static constexpr int_t limbCount = (wordCount - 1) / 2;
    //! Type of the signed top, which is a single word for an odd number of words
    using Top = std::conditional_t<wordCount % 2 == 0, int64_t, int32_t>;
    using Words = fixed_point_detail::Words<wordCount>;

    //! Constructs zero
    constexpr FixedPoint() = default;

    //! Constructs from the two's complement words of the scaled value in little endian order
    static constexpr FixedPoint fromWords(const Words& words) {
        FixedPoint result;
        result.m_words = words;
        return result;
    }

    //! Constructs from an integer type whose range is always representable
    template<std::integral T>
        requires(L.contains(integerLayoutFor<T>()))
    static constexpr FixedPoint fromInt(T value) {
        using Integer = FixedPoint<integerLayoutFor<T>()>;
        Integer integer;
        if constexpr (Integer::limbCount == 0) {
            integer.setTop(static_cast<typename Integer::Top>(value));
        } else {
            // Only unsigned 64-bit integers need a limb, the top stays zero
            integer.setLimb(0, static_cast<uint64_t>(value));
        }
        return integer.template widen<L>();
    }

    static constexpr FixedPoint minValue() {
        FixedPoint result;
        result.setTop(std::numeric_limits<Top>::min());
        return result;
    }

    static constexpr FixedPoint maxValue() {
        FixedPoint result;
        for (int_t i = 0; i < limbCount; i++)
            result.setLimb(i, ~uint64_t(0));
        result.setTop(std::numeric_limits<Top>::max());
        return result;
    }

    //! Returns the smallest positive number 2^(-32 * fractionWords)
    static constexpr FixedPoint ulp() {
        FixedPoint result;
        if constexpr (limbCount > 0)
            result.setLimb(0, 1);
        else
            result.setTop(1);
        return result;
    }

    constexpr uint32_t getWord(int_t index) const { return m_words[index]; }
    constexpr Words asWords() const { return m_words; }

    //! Returns the unsigned limb consisting of the words 2 * index and 2 * index + 1
    constexpr uint64_t getLimb(int_t index) const {
        VERIFY(index >= 0 && index < limbCount);
        return getWordPair(2 * index);
    }

    constexpr void setLimb(int_t index, uint64_t limb) {
        VERIFY(index >= 0 && index < limbCount);
        setWordPair(2 * index, limb);
    }

    constexpr Top getTop() const {
        if constexpr (wordCount % 2 == 0)
            return static_cast<Top>(getWordPair(wordCount - 2));
        else
            return static_cast<Top>(m_words[wordCount - 1]);
    }

    constexpr void setTop(Top top) {
        if constexpr (wordCount % 2 == 0)
            setWordPair(wordCount - 2, static_cast<uint64_t>(top));
        else
            m_words[wordCount - 1] = static_cast<uint32_t>(top);
    }

    constexpr bool isNegative() const { return getTop() < 0; }

    constexpr bool isZero() const {
        bool result = getTop() == 0;
        for (int_t i = 0; i < limbCount; i++)
            result &= getLimb(i) == 0;
        return result;
    }

    constexpr int sign() const {
        return isNegative() ? -1 : (isZero() ? 0 : 1);
    }

    //! Returns the largest integer less than or equal to this number, which is always representable
    constexpr FixedPoint floor() const {
        return maskLowBits(32 * L.fractionWords, true);
    }

    //! Returns this number minus its floor, which is in [0, 1)
    constexpr FixedPoint fractionalPart() const {
        return maskLowBits(32 * L.fractionWords, false);
    }

    //! Exactly converts to a layout which contains this layout
    template<FixedPointLayout To>
        requires(To.contains(L))
    constexpr FixedPoint<To> widen() const {
        using namespace fixed_point_detail;
        FixedPoint<To> result;
        shiftOverflow<To.fractionWords - L.fractionWords>(*this, result);
        return result;
    }

    //! Returns the value scaled by 2^(32 * fractionWords), which is an integer
    BigInt scaledBigInt() const {
        BigInt result = getTop();
        BigInt base = BigInt::forUInt128(static_cast<unsigned __int128>(1) << 64);
        for (int_t i = limbCount - 1; i >= 0; i--)
            result = result * base + BigInt(getLimb(i));
        return result;
    }

    friend constexpr bool operator==(const FixedPoint&, const FixedPoint&) = default;

    friend constexpr std::strong_ordering operator<=>(const FixedPoint& a, const FixedPoint& b) {
        if (a.getTop() != b.getTop())
            return a.getTop() <=> b.getTop();
        for (int_t i = limbCount - 1; i >= 0; i--) {
            if (a.getLimb(i) != b.getLimb(i))
                return a.getLimb(i) <=> b.getLimb(i);
        }
        return std::strong_ordering::equal;
    }

private:
    // Copying the words compiles to a single 64-bit access, while compilers do not merge separate stores of the
    // words. This requires that the words are stored in little endian order.
    static_assert(std::endian::native == std::endian::little);

    constexpr uint64_t getWordPair(int_t index) const {
        if consteval {
            return m_words[index] | (static_cast<uint64_t>(m_words[index + 1]) << 32);
        } else {
            uint64_t result;
            __builtin_memcpy(&result, &m_words[index], sizeof(result));
            return result;
        }
    }

    constexpr void setWordPair(int_t index, uint64_t value) {
        if consteval {
            m_words[index] = static_cast<uint32_t>(value);
            m_words[index + 1] = static_cast<uint32_t>(value >> 32);
        } else {
            __builtin_memcpy(&m_words[index], &value, sizeof(value));
        }
    }

    //! Keeps the lowest bits, or all other bits if invert is set
    constexpr FixedPoint maskLowBits(int_t bits, bool invert) const {
        using namespace fixed_point_detail;
        uint64_t flip = invert ? ~uint64_t(0) : 0;
        FixedPoint result;
        for (int_t i = 0; i < limbCount; i++)
            result.setLimb(i, getLimb(i) & (lowBitsMask(i, bits) ^ flip));
        uint64_t topMask = lowBitsMask(limbCount, bits) ^ flip;
        result.setTop(static_cast<Top>(topLimb(*this) & topMask));
        return result;
    }

    Words m_words {};
};

//! Computes out = a + b and returns whether the result overflowed
template<FixedPointLayout L>
[[nodiscard]] bool addOverflow(const FixedPoint<L>& a, const FixedPoint<L>& b, FixedPoint<L>& out) {
    using namespace fixed_point_detail;
    using Top = typename FixedPoint<L>::Top;
    FixedPoint<L> result;
    uint64_t carry = 0;
    for (int_t i = 0; i < FixedPoint<L>::limbCount; i++)
        result.setLimb(i, addCarry(a.getLimb(i), b.getLimb(i), carry));
    // The overflow of the signed top is the overflow of the number. Both steps only overflow together if the carry
    // wraps the partial sum back into range, so the overflows combine with xor.
    Top partial, top;
    bool overflow = __builtin_add_overflow(a.getTop(), b.getTop(), &partial);
    overflow ^= __builtin_add_overflow(partial, static_cast<Top>(carry), &top);
    result.setTop(top);
    out = result;
    return overflow;
}

//! Computes out = a - b and returns whether the result overflowed
template<FixedPointLayout L>
[[nodiscard]] bool subOverflow(const FixedPoint<L>& a, const FixedPoint<L>& b, FixedPoint<L>& out) {
    using namespace fixed_point_detail;
    using Top = typename FixedPoint<L>::Top;
    FixedPoint<L> result;
    uint64_t borrow = 0;
    for (int_t i = 0; i < FixedPoint<L>::limbCount; i++)
        result.setLimb(i, subBorrow(a.getLimb(i), b.getLimb(i), borrow));
    // The overflow of the signed top is the overflow of the number. Both steps only overflow together if the borrow
    // wraps the partial difference back into range, so the overflows combine with xor.
    Top partial, top;
    bool overflow = __builtin_sub_overflow(a.getTop(), b.getTop(), &partial);
    overflow ^= __builtin_sub_overflow(partial, static_cast<Top>(borrow), &top);
    result.setTop(top);
    out = result;
    return overflow;
}

//! Computes out = -a and returns whether the result overflowed, which only happens for the minimum value
template<FixedPointLayout L>
[[nodiscard]] bool negateOverflow(const FixedPoint<L>& a, FixedPoint<L>& out) {
    return subOverflow(FixedPoint<L>(), a, out);
}

//! Computes the exact product
/*!
All partial products are unsigned except for the product of the signed tops. Treating the top of a as unsigned
adds 2^64 to it for negative a, so the result is corrected by subtracting the limbs of b shifted to the top of a.
Likewise for b. The product of the tops needs no correction because the signed product already accounts for the
signs.
*/
template<FixedPointLayout A, FixedPointLayout B>
FixedPoint<productLayout(A, B)> multiply(const FixedPoint<A>& a, const FixedPoint<B>& b) {
    using namespace fixed_point_detail;
    using Product = FixedPoint<productLayout(A, B)>;
    constexpr int_t limbsA = FixedPoint<A>::limbCount;
    constexpr int_t limbsB = FixedPoint<B>::limbCount;
    // The limbs including the tops sign extended to limbs
    Limbs<limbsA + limbsB + 2> limbs {};
    for (int_t i = 0; i <= limbsA; i++) {
        uint64_t limbA = extendedLimb(a, i);
        uint64_t carry = 0;
        for (int_t j = 0; j <= limbsB; j++) {
            unsigned __int128 product;
            if (i == limbsA && j == limbsB)
                product = static_cast<__int128>(a.getTop()) * b.getTop();
            else
                product = static_cast<unsigned __int128>(limbA) * extendedLimb(b, j);
            unsigned __int128 t = product + limbs[i + j] + carry;
            limbs[i + j] = static_cast<uint64_t>(t);
            carry = static_cast<uint64_t>(t >> 64);
        }
        limbs[i + limbsB + 1] = carry;
    }

    uint64_t maskA = signLimb(topLimb(a));
    uint64_t borrow = 0;
    for (int_t j = 0; j <= limbsB; j++)
        limbs[limbsA + 1 + j] = subBorrow(limbs[limbsA + 1 + j], j < limbsB ? b.getLimb(j) & maskA : 0, borrow);
    uint64_t maskB = signLimb(topLimb(b));
    borrow = 0;
    for (int_t i = 0; i <= limbsA; i++)
        limbs[limbsB + 1 + i] = subBorrow(limbs[limbsB + 1 + i], i < limbsA ? a.getLimb(i) & maskB : 0, borrow);

    Product result;
    for (int_t i = 0; i < Product::limbCount; i++)
        result.setLimb(i, limbs[i]);
    result.setTop(static_cast<typename Product::Top>(limbs[Product::limbCount]));
    return result;
}

//! Converts between arbitrary layouts and returns whether the result overflowed
template<RoundingMode mode, FixedPointLayout From, FixedPointLayout To>
[[nodiscard]] bool convertOverflow(const FixedPoint<From>& in, FixedPoint<To>& out) {
    using namespace fixed_point_detail;
    constexpr int_t shift = int_t(To.fractionWords) - int_t(From.fractionWords);
    if constexpr (shift >= 0) {
        // Exact apart from overflow
        return shiftOverflow<shift>(in, out);
    } else {
        // Truncating the dropped words rounds down, so a bias is added first which carries into the kept words
        // exactly if the mode rounds up. The additional integer word keeps the sum from overflowing.
        using Wide = FixedPoint<FixedPointLayout { From.integerWords + 1, From.fractionWords }>;
        constexpr int_t droppedBits = 32 * -shift;
        bool odd = (wordsAt(in, -shift) & 1) != 0;
        Wide bias;
        for (int_t i = 0; i < Wide::limbCount; i++) {
            // Adding 2^droppedBits - 1 rounds up if the dropped bits are not zero, while adding
            // 2^(droppedBits - 1) - 1 rounds up above the half and adding 2^(droppedBits - 1) also at the half
            uint64_t inexact = lowBitsMask(i, droppedBits);
            uint64_t aboveHalf = lowBitsMask(i, droppedBits - 1);
            uint64_t limb = 0;
            switch (mode) {
            case RoundingMode::Floor:
                break;
            case RoundingMode::Ceil:
                limb = inexact;
                break;
            case RoundingMode::TowardZero:
                limb = in.isNegative() ? inexact : 0;
                break;
            case RoundingMode::Nearest:
                limb = odd ? inexact ^ aboveHalf : aboveHalf;
                break;
            }
            bias.setLimb(i, limb);
        }
        Wide sum = in.template widen<Wide::layout>();
        addSignExtended(sum, bias);
        return shiftOverflow<shift>(sum, out);
    }
}

//! Exact accumulator for sums of products, such as dot products
/*!
Holds the exact sum of up to 2^32 terms, where each term is a product of numbers with layouts A and B or a
number whose layout is contained in productLayout(A, B). With this bound the accumulator cannot overflow, so a
computation like a matrix-vector product only rounds once per output entry.
*/
template<FixedPointLayout A, FixedPointLayout B = A>
class Accumulator {
public:
    static constexpr FixedPointLayout layout = dotLayout(A, B);

    //! Constructs zero
    Accumulator() = default;

    void addProduct(const FixedPoint<A>& a, const FixedPoint<B>& b) {
        using namespace fixed_point_detail;
        addSignExtended(m_value, multiply(a, b));
    }

    void subProduct(const FixedPoint<A>& a, const FixedPoint<B>& b) {
        using namespace fixed_point_detail;
        subSignExtended(m_value, multiply(a, b));
    }

    template<FixedPointLayout L>
        requires(productLayout(A, B).contains(L))
    void add(const FixedPoint<L>& x) {
        using namespace fixed_point_detail;
        addSignExtended(m_value, x.template widen<productLayout(A, B)>());
    }

    template<FixedPointLayout L>
        requires(productLayout(A, B).contains(L))
    void sub(const FixedPoint<L>& x) {
        using namespace fixed_point_detail;
        subSignExtended(m_value, x.template widen<productLayout(A, B)>());
    }

    bool isNegative() const { return m_value.isNegative(); }

    bool isZero() const { return m_value.isZero(); }

    //! Returns the exact value
    const FixedPoint<layout>& value() const { return m_value; }

    //! Rounds the value into out and returns whether the result overflowed
    template<RoundingMode mode, FixedPointLayout L>
    [[nodiscard]] bool roundOverflow(FixedPoint<L>& out) const {
        return convertOverflow<mode>(value(), out);
    }

private:
    FixedPoint<layout> m_value;
};

template<FixedPointLayout L>
void PrintTo(const FixedPoint<L>& value, std::ostream* os) {
    *os << "FixedPoint<" << L.integerWords << ", " << L.fractionWords << ">(";
    auto words = value.asWords();
    for (int_t i = std::ssize(words) - 1; i >= 0; i--) {
        *os << std::format("{:08x}", words[i]);
        if (i == L.fractionWords && i != 0)
            *os << '.';
        else if (i != 0)
            *os << '\'';
    }
    *os << ')';
}

}
