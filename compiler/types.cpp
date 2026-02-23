#include <types.h>

#include <gtest/gtest.h>

uint8_t hex_value4(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    VERIFY_NOT_REACHED(); // TODO: Should not verify on user input
}

uint8_t hex_value8(FixedString<2> str) {
    return (hex_value4(str[0]) << 4) + (hex_value4(str[1]) << 0);
}

uint16_t hex_value16(FixedString<4> str) {
    return (hex_value4(str[0]) << 12) + (hex_value4(str[1]) << 8)
        + (hex_value4(str[2]) << 4) + (hex_value4(str[3]) << 0);
}

char hex_string4(uint8_t val) {
    static constexpr std::array chars = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
    static_assert(sizeof(chars) == 16);
    return chars[val];
}

FixedString<2> hex_string8(uint8_t val) {
    return { {
        hex_string4((val >> 4) & 0xf),
        hex_string4((val >> 0) & 0xf),
    } };
}

FixedString<4> hex_string16(uint16_t val) {
    return { {
        hex_string4((val >> 12) & 0xf),
        hex_string4((val >> 8) & 0xf),
        hex_string4((val >> 4) & 0xf),
        hex_string4((val >> 0) & 0xf),
    } };
}

// https://datatracker.ietf.org/doc/html/rfc3986#section-2.3
static bool is_unreserved(char c) {
    return (c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9')
        || c == '-' || c == '.' || c == '_' || c == '~';
}

std::string uri_encode(std::string_view in) {
    std::string out;
    for (auto c : in) {
        if (is_unreserved(c)) {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.append(hex_string8(c));
        }
    }
    return out;
}

std::string uri_decode(std::string_view in) {
    std::string out;
    for (auto it = in.begin(); it != in.end(); ++it) {
        if (*it == '%') {
            VERIFY(in.end() - it >= 3); // TODO: Should not verify on user input
            out.push_back(hex_value8({{ it[1], it[2] } }));
            it += 2;
        } else
            out.push_back(*it);
    }
    return out;
}

TEST(Utility, UriEncodeDecode) {
    std::string testString = " !\"#$%&'()*+,-./0123456789:;<=>?"
                             "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
                             "`abcdefghijklmnopqrstuvwxyz{|}~";
    std::string testEnc = "%20%21%22%23%24%25%26%27%28%29%2A%2B%2C-.%2F0123456789%3A%3B%3C%3D%3E%3F"
                          "%40ABCDEFGHIJKLMNOPQRSTUVWXYZ%5B%5C%5D%5E_"
                          "%60abcdefghijklmnopqrstuvwxyz%7B%7C%7D~";
    auto encoded = uri_encode(testString);
    EXPECT_EQ(encoded, testEnc);
    auto decoded = uri_decode(encoded);
    EXPECT_EQ(decoded, testString);
}