#include <verify/backend/integer/FixedPoint.h>

#include <gtest/gtest.h>

#include <random>

namespace verify::backend::integer {

namespace {

    using Rng = std::mt19937_64;

    BigInt pow2(uint32_t exponent) {
        BigInt result = int64_t(1) << (exponent % 32);
        BigInt base = int64_t(1) << 32;
        for (uint32_t i = 0; i < exponent / 32; i++)
            result = result * base;
        return result;
    }

    //! Returns whether the scaled value fits into the given number of words
    bool fitsWords(const BigInt& scaled, uint32_t words) {
        BigInt limit = pow2(32 * words - 1);
        return scaled >= -limit && scaled < limit;
    }

    //! Random words biased towards boundary cases of the arithmetic
    template<int_t N>
    fixed_point_detail::Words<N> randomWords(Rng& rng) {
        constexpr uint32_t specialWords[] = { 0, 1, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFE, 0xFFFFFFFF };
        fixed_point_detail::Words<N> words;
        switch (rng() % 4) {
        case 0:
            for (uint32_t& word : words)
                word = static_cast<uint32_t>(rng());
            break;
        case 1: {
            // A random number of significant words, then sign extension
            int_t significant = 1 + rng() % N;
            for (int_t i = 0; i < significant; i++)
                words[i] = static_cast<uint32_t>(rng());
            for (int_t i = significant; i < N; i++)
                words[i] = static_cast<uint32_t>(static_cast<int32_t>(words[significant - 1]) >> 31);
            break;
        }
        case 2:
            for (uint32_t& word : words)
                word = specialWords[rng() % std::size(specialWords)];
            break;
        default:
            for (uint32_t& word : words)
                word = rng() % 2 ? specialWords[rng() % std::size(specialWords)] : static_cast<uint32_t>(rng());
            break;
        }
        return words;
    }

    template<FixedPointLayout L>
    FixedPoint<L> randomFixed(Rng& rng) {
        return FixedPoint<L>::fromWords(randomWords<L.totalWords()>(rng));
    }

    //! Returns the ordering of q compared to the rational number numerator / denominator
    std::strong_ordering compareToRatio(const BigInt& q, const BigInt& numerator, const BigInt& denominator) {
        auto order = q * denominator <=> numerator;
        return denominator < 0 ? 0 <=> order : order;
    }

    //! Checks that q is numerator / denominator rounded according to the mode
    ::testing::AssertionResult isRounded(const BigInt& q, bool qOdd, const BigInt& numerator, const BigInt& denominator, RoundingMode mode) {
        auto floorHolds = [&] {
            return compareToRatio(q, numerator, denominator) <= 0 && compareToRatio(q + 1, numerator, denominator) > 0;
        };
        auto ceilHolds = [&] {
            return compareToRatio(q, numerator, denominator) >= 0 && compareToRatio(q - 1, numerator, denominator) < 0;
        };
        bool result = false;
        switch (mode) {
        case RoundingMode::Floor:
            result = floorHolds();
            break;
        case RoundingMode::Ceil:
            result = ceilHolds();
            break;
        case RoundingMode::TowardZero:
            result = compareToRatio(0, numerator, denominator) < 0 ? floorHolds() : ceilHolds();
            break;
        case RoundingMode::Nearest: {
            // (2q - 1) / 2 <= numerator / denominator <= (2q + 1) / 2 with q even at the boundaries
            auto lower = compareToRatio(2 * q - 1, 2 * numerator, denominator);
            auto upper = compareToRatio(2 * q + 1, 2 * numerator, denominator);
            result = lower <= 0 && upper >= 0 && ((lower != 0 && upper != 0) || !qOdd);
            break;
        }
        }
        if (result)
            return ::testing::AssertionSuccess();
        return ::testing::AssertionFailure() << "incorrect rounding with mode " << static_cast<int>(mode);
    }

    template<FixedPointLayout L>
    void checkAddSub(Rng& rng, int iterations) {
        for (int i = 0; i < iterations; i++) {
            auto a = randomFixed<L>(rng);
            auto b = randomFixed<L>(rng);
            SCOPED_TRACE(::testing::PrintToString(a) + " and " + ::testing::PrintToString(b));

            FixedPoint<L> sum;
            BigInt expectedSum = a.scaledBigInt() + b.scaledBigInt();
            bool sumOverflow = addOverflow(a, b, sum);
            EXPECT_EQ(sumOverflow, !fitsWords(expectedSum, L.totalWords()));
            if (!sumOverflow)
                EXPECT_EQ(sum.scaledBigInt(), expectedSum);

            FixedPoint<L> difference;
            BigInt expectedDifference = a.scaledBigInt() - b.scaledBigInt();
            bool differenceOverflow = subOverflow(a, b, difference);
            EXPECT_EQ(differenceOverflow, !fitsWords(expectedDifference, L.totalWords()));
            if (!differenceOverflow)
                EXPECT_EQ(difference.scaledBigInt(), expectedDifference);

            FixedPoint<L> negated;
            bool negateOverflowed = negateOverflow(a, negated);
            EXPECT_EQ(negateOverflowed, a == FixedPoint<L>::minValue());
            if (!negateOverflowed)
                EXPECT_EQ(negated.scaledBigInt(), -a.scaledBigInt());

            EXPECT_EQ(a <=> b, a.scaledBigInt() <=> b.scaledBigInt());
            EXPECT_EQ(a.sign(), (a.scaledBigInt() <=> 0) < 0 ? -1 : (a.isZero() ? 0 : 1));
        }
    }

    template<FixedPointLayout A, FixedPointLayout B>
    void checkMultiply(Rng& rng, int iterations) {
        for (int i = 0; i < iterations; i++) {
            auto a = randomFixed<A>(rng);
            auto b = randomFixed<B>(rng);
            SCOPED_TRACE(::testing::PrintToString(a) + " and " + ::testing::PrintToString(b));
            EXPECT_EQ(multiply(a, b).scaledBigInt(), a.scaledBigInt() * b.scaledBigInt());
        }
        EXPECT_EQ(multiply(FixedPoint<A>::minValue(), FixedPoint<B>::minValue()).scaledBigInt(),
            FixedPoint<A>::minValue().scaledBigInt() * FixedPoint<B>::minValue().scaledBigInt());
    }

    template<FixedPointLayout A, FixedPointLayout B>
    void checkAccumulator(Rng& rng, int iterations) {
        for (int i = 0; i < iterations; i++) {
            Accumulator<A, B> accumulator;
            BigInt expected;
            int_t terms = rng() % 64;
            for (int_t j = 0; j < terms; j++) {
                auto a = randomFixed<A>(rng);
                auto b = randomFixed<B>(rng);
                BigInt product = a.scaledBigInt() * b.scaledBigInt();
                switch (rng() % 4) {
                case 0:
                    accumulator.addProduct(a, b);
                    expected = expected + product;
                    break;
                case 1:
                    accumulator.subProduct(a, b);
                    expected = expected - product;
                    break;
                case 2:
                    // Scale to the fraction words of the accumulator
                    accumulator.add(a);
                    expected = expected + a.scaledBigInt() * pow2(32 * B.fractionWords);
                    break;
                default:
                    accumulator.sub(a);
                    expected = expected - a.scaledBigInt() * pow2(32 * B.fractionWords);
                    break;
                }
            }
            EXPECT_EQ(accumulator.value().scaledBigInt(), expected);
            EXPECT_EQ(accumulator.isZero(), expected == 0);
            EXPECT_EQ(accumulator.isNegative(), expected < 0);
        }
    }

    //! Checks conversion against an exact reference, which is a conversion to a layout where it cannot overflow
    template<RoundingMode mode, FixedPointLayout From, FixedPointLayout To>
    void checkConvert(Rng& rng, int iterations) {
        constexpr FixedPointLayout Wide = { std::max(From.integerWords, To.integerWords) + 1, To.fractionWords };
        for (int i = 0; i < iterations; i++) {
            auto in = randomFixed<From>(rng);
            SCOPED_TRACE(::testing::PrintToString(in));
            FixedPoint<Wide> reference;
            EXPECT_FALSE(convertOverflow<mode>(in, reference));
            BigInt scaled = reference.scaledBigInt();
            if constexpr (To.fractionWords >= From.fractionWords) {
                EXPECT_EQ(scaled, in.scaledBigInt() * pow2(32 * (To.fractionWords - From.fractionWords)));
            } else {
                BigInt denominator = pow2(32 * (From.fractionWords - To.fractionWords));
                EXPECT_TRUE(isRounded(scaled, reference.getWord(0) & 1, in.scaledBigInt(), denominator, mode));
            }

            FixedPoint<To> out;
            bool overflow = convertOverflow<mode>(in, out);
            EXPECT_EQ(overflow, !fitsWords(scaled, To.totalWords()));
            if (!overflow)
                EXPECT_EQ(out.scaledBigInt(), scaled);
        }
    }
}

TEST(VerifyBackendInteger, LayoutAlgebra) {
    constexpr FixedPointLayout baseline = { 2, 1 };
    static_assert(productLayout(baseline, baseline) == FixedPointLayout { 4, 2 });
    static_assert(dotLayout(baseline, baseline) == FixedPointLayout { 5, 2 });
    static_assert(productLayout({ 2, 0 }, baseline) == FixedPointLayout { 4, 1 });
    static_assert(FixedPointLayout { 3, 2 }.contains(baseline));
    static_assert(!FixedPointLayout { 3, 0 }.contains(baseline));

    // A baseline entry of a sparse matrix fits into 128 bits
    struct Entry {
        uint32_t index;
        FixedPoint<FixedPointLayout { 2, 1 }> value;
    };
    static_assert(sizeof(FixedPoint<baseline>) == 12);
    static_assert(alignof(FixedPoint<baseline>) == 4);
    static_assert(sizeof(Entry) == 16);
}

template<RoundingMode mode>
static auto round(double value) {
    using F = FixedPoint<FixedPointLayout { 1, 1 }>;
    using I = FixedPoint<FixedPointLayout { 1, 0 }>;
    auto in = F::fromWords({ static_cast<uint32_t>(static_cast<int64_t>(std::ldexp(value, 32))),
        static_cast<uint32_t>(static_cast<int64_t>(std::floor(value))) });
    I out;
    bool overflow = convertOverflow<mode>(in, out);
    VERIFY(!overflow);
    return out.getWord(0);
}

TEST(VerifyBackendInteger, FixedPointRoundingTies) {
    EXPECT_EQ(round<RoundingMode::Nearest>(2.5), 2);
    EXPECT_EQ(round<RoundingMode::Nearest>(3.5), 4);
    EXPECT_EQ(round<RoundingMode::Nearest>(-2.5), -2);
    EXPECT_EQ(round<RoundingMode::Nearest>(-3.5), -4);
    EXPECT_EQ(round<RoundingMode::Nearest>(-2.25), -2);
    EXPECT_EQ(round<RoundingMode::Nearest>(-2.75), -3);
    EXPECT_EQ(round<RoundingMode::Floor>(-2.5), -3);
    EXPECT_EQ(round<RoundingMode::Ceil>(-2.5), -2);
    EXPECT_EQ(round<RoundingMode::TowardZero>(-2.5), -2);
    EXPECT_EQ(round<RoundingMode::TowardZero>(2.5), 2);

    // Rounding the maximum value up overflows
    using F = FixedPoint<FixedPointLayout { 1, 1 }>;
    using I = FixedPoint<FixedPointLayout { 1, 0 }>;
    I out;
    EXPECT_TRUE(convertOverflow<RoundingMode::Ceil>(F::maxValue(), out));
    EXPECT_FALSE(convertOverflow<RoundingMode::Floor>(F::maxValue(), out));
    EXPECT_EQ(out, I::maxValue());
}

TEST(VerifyBackendInteger, FixedPointAddSub) {
    Rng rng(1);
    checkAddSub<FixedPointLayout { 1, 0 }>(rng, 2000);
    checkAddSub<FixedPointLayout { 1, 1 }>(rng, 2000);
    checkAddSub<FixedPointLayout { 2, 1 }>(rng, 2000);
    checkAddSub<FixedPointLayout { 2, 2 }>(rng, 2000);
    checkAddSub<FixedPointLayout { 3, 2 }>(rng, 2000);
}

TEST(VerifyBackendInteger, FixedPointCarryIntoTopLimb) {
    using F = FixedPoint<FixedPointLayout { 2, 2 }>;
    F out;

    // The top limbs overflow on their own, but the carry from the lower limb brings the sum back into range
    auto a = F::fromWords({ 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x80000000 });
    auto b = F::fromWords({ 0x00000001, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF });
    EXPECT_FALSE(addOverflow(a, b, out));
    EXPECT_EQ(out, F::minValue());
    EXPECT_TRUE(addOverflow(a, F::fromWords({ 0, 0, 0xFFFFFFFF, 0xFFFFFFFF }), out));

    // Likewise for the borrow
    auto c = F::fromWords({ 0x00000000, 0x00000000, 0xFFFFFFFF, 0x7FFFFFFF });
    auto d = F::fromWords({ 0x00000001, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF });
    EXPECT_FALSE(subOverflow(c, d, out));
    EXPECT_EQ(out, F::maxValue());
    EXPECT_TRUE(subOverflow(c, F::fromWords({ 0, 0, 0xFFFFFFFF, 0xFFFFFFFF }), out));
}

TEST(VerifyBackendInteger, FixedPointMultiply) {
    Rng rng(2);
    checkMultiply<FixedPointLayout { 1, 0 }, FixedPointLayout { 1, 0 }>(rng, 2000);
    checkMultiply<FixedPointLayout { 2, 1 }, FixedPointLayout { 2, 1 }>(rng, 2000);
    checkMultiply<FixedPointLayout { 2, 0 }, FixedPointLayout { 2, 1 }>(rng, 2000);
    checkMultiply<FixedPointLayout { 1, 1 }, FixedPointLayout { 3, 2 }>(rng, 2000);
    checkMultiply<FixedPointLayout { 3, 3 }, FixedPointLayout { 2, 2 }>(rng, 2000);
}

TEST(VerifyBackendInteger, FixedPointAccumulator) {
    Rng rng(3);
    checkAccumulator<FixedPointLayout { 2, 1 }, FixedPointLayout { 2, 1 }>(rng, 300);
    checkAccumulator<FixedPointLayout { 2, 0 }, FixedPointLayout { 2, 1 }>(rng, 300);
    checkAccumulator<FixedPointLayout { 1, 0 }, FixedPointLayout { 1, 0 }>(rng, 300);
    checkAccumulator<FixedPointLayout { 1, 2 }, FixedPointLayout { 2, 1 }>(rng, 300);

    // Products of minimum values have the largest magnitude
    constexpr FixedPointLayout baseline = { 2, 1 };
    auto min = FixedPoint<baseline>::minValue();
    Accumulator<baseline> accumulator;
    for (int i = 0; i < 1000; i++)
        accumulator.addProduct(min, min);
    EXPECT_EQ(accumulator.value().scaledBigInt(), 1000 * min.scaledBigInt() * min.scaledBigInt());
}

template<RoundingMode... modes>
void fixedPointConvertModes(Rng& rng) {
    (checkConvert<modes, FixedPointLayout { 5, 2 }, FixedPointLayout { 2, 1 }>(rng, 1000), ...);
    (checkConvert<modes, FixedPointLayout { 2, 1 }, FixedPointLayout { 5, 2 }>(rng, 1000), ...);
    (checkConvert<modes, FixedPointLayout { 2, 2 }, FixedPointLayout { 1, 1 }>(rng, 1000), ...);
    (checkConvert<modes, FixedPointLayout { 2, 1 }, FixedPointLayout { 3, 0 }>(rng, 1000), ...);
    (checkConvert<modes, FixedPointLayout { 1, 1 }, FixedPointLayout { 1, 0 }>(rng, 1000), ...);
    (checkConvert<modes, FixedPointLayout { 3, 1 }, FixedPointLayout { 1, 3 }>(rng, 1000), ...);
    (checkConvert<modes, FixedPointLayout { 1, 3 }, FixedPointLayout { 3, 1 }>(rng, 1000), ...);
}

TEST(VerifyBackendInteger, FixedPointConvert) {
    Rng rng(4);
    fixedPointConvertModes<RoundingMode::Floor, RoundingMode::Ceil, RoundingMode::TowardZero, RoundingMode::Nearest>(rng);
}

}
