#pragma once

#include <IntOr.h>

#include <array>
#include <compare>
#include <concepts>
#include <span>
#include <utility>

namespace bigint_detail {

using Limb = uint64_t;

//! Big int inline data with storage for numbers in [-2^126, 2^126)
/*!
This helper class implements the inline data and its accessors without implementing any owning semantics.

Inline representation:
    low_bits_or_ptr holds the low 63 bits of the number as integer, high_bits_or_size holds the number
    shifted right by 63 bits (sign extended). Together they form a 127-bit two's complement number.

Allocated representation:
    low_bits_or_ptr holds a pointer to the limbs, which store the magnitude in little endian order.
    high_bits_or_size holds (signedLimbCount << 32) | capacity. The sign of signedLimbCount is the sign of the
    number and its absolute value the number of limbs in use.

Invariants of the allocated representation:
* The number is outside of [-2^126, 2^126), so each number has exactly one representation.
* The most significant limb is not zero, so 2 <= limbCount() <= capacity().

In both representations the highest bit of high_bits_or_size is the sign bit of the number.
*/
struct Fields {
    static constexpr __int128_t inlineLimit = static_cast<__int128_t>(1) << 126;
    //! Maximum number of limbs, such that the signed limb count fits into 32 bits
    static constexpr uint32_t maxLimbs = std::numeric_limits<int32_t>::max();

    static bool fitsInlineInt128(__int128_t value) {
        return value >= -inlineLimit && value < inlineLimit;
    }

    static bool fitsInlineUInt128(__uint128_t value) {
        return value < inlineLimit;
    }

    static Fields makeInlineUnchecked(__int128_t value) {
        return {
            UInt64Or<Limb*>::forInt(static_cast<uint64_t>(value)),
            static_cast<uint64_t>(value >> 63),
        };
    }

    static Fields makeAllocated(uint32_t capacity);

    static Fields makeFittingCopy(const Fields& other);

    //! Deallocate the limbs array and leave the fields in a stale state
    void deallocateLimbs();

    //! Returns whether the number is less than zero
    bool isNegative() const {
        return static_cast<int64_t>(high_bits_or_size) < 0;
    }

    //! Returns whether the number is stored inline
    /*!
    Numbers in [-2^126, 2^126) will be stored inline.
    */
    bool storedInline() const {
        return low_bits_or_ptr.isInt();
    }

    //! Returns whether the number is in [-2^62, 2^62)
    /*!
    Tiny numbers are always stored inline and get a fast path in multiply operations.
    */
    bool tiny() const {
        // Test if the high bits are a sign extension of the low bits.
        // For allocated values this is also false because the highest bit of pointer is
        // always 0 and the size field is never 0.
        return high_bits_or_size == static_cast<uint64_t>(static_cast<int64_t>(low_bits_or_ptr.rawBits()) >> 63);
    }

    __int128_t inlineValue() const {
        VERIFY(storedInline());
        return scaledBits() >> 1;
    }

    //! Returns the fields as one 128-bit integer, which is twice the value for inline numbers
    /*!
    The inline range maps exactly onto the range of __int128_t, so signed overflow of operations
    on scaled values is equivalent to the result not fitting inline.
    */
    __int128_t scaledBits() const {
        return std::bit_cast<__int128_t>(*this);
    }

    //! Inverse of scaledBits(), the scaled value must be even
    static Fields fromScaled(__int128_t scaled) {
        return std::bit_cast<Fields>(scaled);
    }

    static bool bothInline(const Fields& a, const Fields& b) {
        return ((a.low_bits_or_ptr.rawBits() | b.low_bits_or_ptr.rawBits()) & 1) == 0;
    }

    //! Returns the scaled product of two tiny numbers
    /*!
    The product of tiny numbers is at most 2^124 in magnitude, so it always fits inline.
    */
    static __int128_t mulTinyScaled(const Fields& a, const Fields& b) {
        auto aScaled = static_cast<int64_t>(a.low_bits_or_ptr.rawBits());
        auto bValue = static_cast<int64_t>(b.low_bits_or_ptr.rawBits()) >> 1;
        return static_cast<__int128_t>(aScaled) * bValue;
    }

    void setInlineUnchecked(__int128_t value) {
        *this = makeInlineUnchecked(value);
    }

    void setLimbCountAndSign(uint32_t limbCount, bool negative) {
        setSignedLimbCount(negative ? -(int32_t)limbCount : (int32_t)limbCount);
    }

    void setSignedLimbCount(int32_t signedLimbCount) {
        high_bits_or_size = packSize(signedLimbCount, capacity());
    }

    //! Returns a span to the limbs, inline numbers are unpacked into scratch
    std::span<const Limb> magnitude(std::array<Limb, 2>& scratch) const;

    //! Returns a span to the limbs, must not be stored inline
    std::span<const Limb> limbView() const { return { limbData(), limbCount() }; }

    static uint64_t packSize(int32_t signedLimbCount, uint32_t capacity) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(signedLimbCount)) << 32) | capacity;
    }
    int32_t signedLimbCount() const {
        VERIFY(!storedInline());
        return static_cast<int32_t>(high_bits_or_size >> 32);
    }
    uint32_t limbCount() const {
        int32_t n = signedLimbCount();
        return n < 0 ? -static_cast<uint32_t>(n) : static_cast<uint32_t>(n);
    }
    uint32_t capacity() const {
        VERIFY(!storedInline());
        return static_cast<uint32_t>(high_bits_or_size);
    }
    Limb* limbData() {
        return low_bits_or_ptr.payload();
    }
    const Limb* limbData() const {
        return low_bits_or_ptr.payload();
    }

    UInt64Or<Limb*> low_bits_or_ptr;
    uint64_t high_bits_or_size;
};

}

struct CHARGE_TRIVIAL_ABI BigInt : private bigint_detail::Fields {
    //! Constructs zero
    BigInt() noexcept
        : Fields { UInt64Or<bigint_detail::Limb*>::forInt(0), 0 } { }

    //! Constructs from a builtin integer, which is always stored inline
    template<std::integral I>
        requires(!std::is_same_v<I, bool> && sizeof(I) <= sizeof(uint64_t))
    BigInt(I value) noexcept
        : BigInt() {
        setInlineUnchecked(static_cast<__int128_t>(value));
    }

    static BigInt forInt128(__int128_t value);
    static BigInt forUInt128(__uint128_t value);

    using Fields::isNegative;

    BigInt(const BigInt& other)
        : Fields(other) {
        if (!storedInline()) [[unlikely]]
            setFields(Fields::makeFittingCopy(other));
    }
    BigInt(BigInt&& other) noexcept
        : Fields(other) {
        other.setInlineUnchecked(0);
    }
    BigInt& operator=(const BigInt& other);
    BigInt& operator=(BigInt&& other) noexcept {
        release();
        setFields(other);
        other.setInlineUnchecked(0);
        return *this;
    }
    ~BigInt() { release(); }

    friend bool operator==(const BigInt& a, const BigInt& b) {
        if (a.storedInline() || b.storedInline()) [[likely]]
            return a.scaledBits() == b.scaledBits();
        return equalSlow(a, b);
    }

    friend std::strong_ordering operator<=>(const BigInt& a, const BigInt& b) {
        if (Fields::bothInline(a, b)) [[likely]]
            return a.scaledBits() <=> b.scaledBits();
        return compareSlow(a, b);
    }

    friend BigInt operator-(const BigInt& a) {
        __int128_t result;
        bool overflow = __builtin_sub_overflow(static_cast<__int128_t>(0), a.scaledBits(), &result);
        if (a.storedInline() && !overflow) [[likely]]
            return fromFields(fromScaled(result));
        return negateSlow(a);
    }
    friend BigInt operator-(BigInt&& a) {
        a.negateInPlace();
        return std::move(a);
    }

    //! Negates the number in place
    void negateInPlace() {
        __int128_t result;
        bool overflow = __builtin_sub_overflow(static_cast<__int128_t>(0), scaledBits(), &result);
        if (storedInline() && !overflow) [[likely]]
            setFields(fromScaled(result));
        else
            negateInPlaceSlow();
    }

    friend BigInt operator+(const BigInt& a, const BigInt& b) {
        __int128_t result;
        bool overflow = __builtin_add_overflow(a.scaledBits(), b.scaledBits(), &result);
        if (bothInline(a, b) && !overflow) [[likely]]
            return fromFields(fromScaled(result));
        return addSlow(a, false, b, false);
    }
    friend BigInt operator+(BigInt&& a, const BigInt& b) {
        a += b;
        return std::move(a);
    }
    friend BigInt operator+(const BigInt& a, BigInt&& b) {
        b += a;
        return std::move(b);
    }
    friend BigInt operator+(BigInt&& a, BigInt&& b) {
        // Reuse the storage of b if only it has an allocation, which happens e.g. for 1 + std::move(b)
        if (a.storedInline() && !b.storedInline()) {
            b += a;
            return std::move(b);
        }
        a += b;
        return std::move(a);
    }

    BigInt& operator+=(const BigInt& b) {
        __int128_t result;
        bool overflow = __builtin_add_overflow(scaledBits(), b.scaledBits(), &result);
        if (bothInline(*this, b) && !overflow) [[likely]]
            setFields(fromScaled(result));
        else
            addAssignSlow(false, b, false);
        return *this;
    }

    friend BigInt operator-(const BigInt& a, const BigInt& b) {
        __int128_t result;
        bool overflow = __builtin_sub_overflow(a.scaledBits(), b.scaledBits(), &result);
        if (bothInline(a, b) && !overflow) [[likely]]
            return fromFields(fromScaled(result));
        return addSlow(a, false, b, true);
    }
    friend BigInt operator-(BigInt&& a, const BigInt& b) {
        a -= b;
        return std::move(a);
    }
    friend BigInt operator-(const BigInt& a, BigInt&& b) {
        b.subtractFrom(a);
        return std::move(b);
    }
    friend BigInt operator-(BigInt&& a, BigInt&& b) {
        if (a.storedInline() && !b.storedInline()) {
            b.subtractFrom(a);
            return std::move(b);
        }
        a -= b;
        return std::move(a);
    }

    BigInt& operator-=(const BigInt& b) {
        __int128_t result;
        bool overflow = __builtin_sub_overflow(scaledBits(), b.scaledBits(), &result);
        if (bothInline(*this, b) && !overflow) [[likely]]
            setFields(fromScaled(result));
        else
            addAssignSlow(false, b, true);
        return *this;
    }

    friend BigInt operator*(const BigInt& a, const BigInt& b) {
        if (a.tiny() && b.tiny()) [[likely]]
            return fromFields(fromScaled(mulTinyScaled(a, b)));
        return mulSlow(a, b);
    }

    BigInt& operator*=(const BigInt& b) {
        if (tiny() && b.tiny()) [[likely]]
            setFields(fromScaled(mulTinyScaled(*this, b)));
        else
            mulAssignSlow(b);
        return *this;
    }

private:
    friend struct BigIntTestAccess;
    using Limb = bigint_detail::Limb;

    //! Creates an allocated number with uninitialized limbs
    /*!
    The number has to be completed with finish() before it is used as a value.
    */
    static BigInt withCapacity(uint32_t capacity);

    //! Allocate a 2-limb number for the given value
    /*!
    Does not call finish(), so value must be known to not fit inline.
    */
    static BigInt allocateMagnitude128(__uint128_t magnitude, bool negative);

    void setFields(const Fields& other) { (Fields&)* this = other; }

    static BigInt fromFields(const Fields& fields) {
        BigInt result;
        result.setFields(fields);
        return result;
    }

    //! Replaces this number with a - this
    void subtractFrom(const BigInt& a) {
        __int128_t result;
        bool overflow = __builtin_sub_overflow(a.scaledBits(), scaledBits(), &result);
        if (bothInline(a, *this) && !overflow) [[likely]]
            setFields(fromScaled(result));
        else
            addAssignSlow(true, a, false);
    }

    // Out of line slow paths for operations where the fast path in the header does not apply

    static bool equalSlow(const BigInt& a, const BigInt& b);
    static std::strong_ordering compareSlow(const BigInt& a, const BigInt& b);
    static BigInt negateSlow(const BigInt& a);
    void negateInPlaceSlow();
    //! Computes (negateA ? -a : a) + (negateB ? -b : b)
    static BigInt addSlow(const BigInt& a, bool negateA, const BigInt& b, bool negateB);
    //! Replaces this number with (negateThis ? -this : this) + (negateB ? -b : b)
    void addAssignSlow(bool negateThis, const BigInt& b, bool negateB);
    //! Multiplies numbers which are not both tiny
    static BigInt mulSlow(const BigInt& a, const BigInt& b);
    void mulAssignSlow(const BigInt& b);
    //! Store the result of the signed addition of a and b into this
    /*!
    Needs room for addCapacity() limbs and may alias a or b.
    */
    void storeSignedAddition(std::span<const Limb> a, bool aNegative, std::span<const Limb> b, bool bNegative);

    //! Completes a number after its magnitude was written into limbData()
    /*!
    Trims leading zero limbs from the count and moves the number inline if it fits.
    */
    void finish(bool negative, uint32_t count);

    //! Grows the allocation of an allocated number to at least minCapacity limbs
    /*!
    Only the first limbCount() limbs are preserved.
    */
    void reserve(uint32_t minCapacity);

    //! Releases any allocated data and leaves the object in a stale state
    /*!
    Callers must in all cases overwrite it with new data. The state is not even valid as a
    move-from state as a later destructor call would attempt to free the data again.
    */
    void release() {
        if (!storedInline()) [[unlikely]]
            deallocateLimbs();
    }
};

static_assert(sizeof(BigInt) == 16);
