#pragma once

#include <IntOr.h>

#include <array>
#include <concepts>

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
        return std::bit_cast<__int128_t>(*this) >> 1;
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

struct BigInt : private bigint_detail::Fields {
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
