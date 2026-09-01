#pragma once

#include <padded_string_compare.h>
#include <types.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

class padded_string_view;

namespace padded_string_detail {
//! Backs the empty views, so that every padded_string_view may be read in whole blocks
inline constexpr std::array<char, PADDED_STRING_PADDING> EMPTY_CONTENT = {};
}

//! An immutable string that carries PADDED_STRING_PADDING readable bytes behind its content
/*!
The padding is zero filled, so the content is always zero terminated.
*/
class padded_string {
public:
    using const_iterator = const char*;

    padded_string()
        : padded_string(std::string_view()) { }
    //! Construct a buffer, always copying the content
    explicit padded_string(std::string_view content);
    padded_string(const padded_string& other)
        : padded_string(std::string_view(other)) { }
    padded_string(padded_string&& other) noexcept
        : m_begin(std::exchange(other.m_begin, nullptr))
        , m_end(std::exchange(other.m_end, nullptr)) { }
    padded_string& operator=(const padded_string& other);
    padded_string& operator=(padded_string&& other) noexcept;
    ~padded_string();

    const char* data() const { return m_begin; }
    int_t size() const { return m_end - m_begin; }
    int_t length() const { return size(); }
    bool empty() const { return m_begin == m_end; }

    const_iterator begin() const { return m_begin; }
    const_iterator end() const { return m_end; }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    //! Indexing the character one past the content is allowed, it reads the terminating '\0'
    const char& operator[](int_t index) const {
        VERIFY(index >= 0 && index <= size());
        return m_begin[index];
    }
    const char& front() const { return (*this)[0]; }
    const char& back() const { return (*this)[size() - 1]; }

    operator std::string_view() const { return { m_begin, m_end }; }
    explicit operator std::string() const { return { m_begin, m_end }; }

private:
    char* m_begin = nullptr;
    char* m_end = nullptr;
};

//! A read only view into the content of a padded_string
class padded_string_view {
public:
    using const_iterator = const char*;

    constexpr padded_string_view()
        : padded_string_view(padded_string_detail::EMPTY_CONTENT.data(), padded_string_detail::EMPTY_CONTENT.data()) { }
    padded_string_view(const padded_string& buffer)
        : m_begin(buffer.begin()), m_end(buffer.end()) { }
    //! Views may not be taken from a temporary, they do not extend its lifetime
    padded_string_view(padded_string&&) = delete;

    [[nodiscard]] static constexpr padded_string_view from_raw_unsafe(std::string_view content) {
        return padded_string_view(content.data(), content.data() + content.size());
    }

    constexpr const char* data() const { return m_begin; }
    constexpr int_t size() const { return m_end - m_begin; }
    constexpr int_t length() const { return size(); }
    constexpr bool empty() const { return m_begin == m_end; }

    constexpr const_iterator begin() const { return m_begin; }
    constexpr const_iterator end() const { return m_end; }
    constexpr const_iterator cbegin() const { return begin(); }
    constexpr const_iterator cend() const { return end(); }

    constexpr const char& operator[](int_t index) const {
        VERIFY(index >= 0 && index < size());
        return m_begin[index];
    }
    constexpr const char& front() const { return (*this)[0]; }
    constexpr const char& back() const { return (*this)[size() - 1]; }

    constexpr padded_string_view substr(int_t offset, int_t count = limits::max) const {
        VERIFY(offset >= 0 && offset <= size());
        int_t remaining = size() - offset;
        if (count > remaining)
            count = remaining;
        return padded_string_view(m_begin + offset, m_begin + offset + count);
    }

    constexpr operator std::string_view() const { return { m_begin, m_end }; }
    explicit operator std::string() const { return { m_begin, m_end }; }

private:
    constexpr padded_string_view(const char* begin, const char* end)
        : m_begin(begin), m_end(end) { }

    const char* m_begin;
    const char* m_end;
};

[[nodiscard]] constexpr bool operator==(padded_string_view a, padded_string_view b) {
    // Both operands are padded, so the comparison may run on whole blocks
    return a.size() == b.size() && padded_string_compare_eq(a.data(), b.data(), a.size());
}
[[nodiscard]] constexpr bool operator==(padded_string_view a, std::string_view b) {
    // Only one operand is padded, so the comparison stays within the content
    return std::string_view(a) == b;
}
// Without these a padded_string operand is equally convertible to either string view type
[[nodiscard]] inline bool operator==(padded_string_view a, const padded_string& b) {
    return a == padded_string_view(b);
}
[[nodiscard]] inline bool operator==(const padded_string& a, const padded_string& b) {
    return padded_string_view(a) == padded_string_view(b);
}
[[nodiscard]] inline bool operator==(const padded_string& a, std::string_view b) {
    return std::string_view(a) == b;
}

template<typename T>
concept StringViewLike = std::same_as<T, std::string_view> || std::same_as<T, padded_string_view>;

inline padded_string::padded_string(std::string_view content) {
    std::allocator<char> allocator;
    m_begin = allocator.allocate(static_cast<size_t>(content.size()) + PADDED_STRING_PADDING);
    m_end = std::copy(content.begin(), content.end(), m_begin);
    std::fill_n(m_end, PADDED_STRING_PADDING, '\0');
}
inline padded_string& padded_string::operator=(const padded_string& other) {
    return *this = padded_string(std::string_view(other));
}
inline padded_string& padded_string::operator=(padded_string&& other) noexcept {
    std::swap(m_begin, other.m_begin);
    std::swap(m_end, other.m_end);
    return *this;
}
inline padded_string::~padded_string() {
    if (m_begin == nullptr)
        return;
    std::allocator<char> allocator;
    allocator.deallocate(m_begin, static_cast<size_t>(size()) + PADDED_STRING_PADDING);
}
