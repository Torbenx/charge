#pragma once

#include <types.h>

#include <algorithm>
#include <utility>

#define JSON_OBJECT        \
    template<int_t index>  \
    struct _json_ref_data; \
    static constexpr int_t _json_base_counter = __COUNTER__;

#define JSON_MEMBER_IMPL(memberName, counter)                                                       \
    static _json_##memberName##_stub();                                                             \
    template<>                                                                                      \
    struct _json_ref_data<counter - _json_base_counter> {                                           \
        using type = decltype(_json_##memberName##_stub());                                         \
        static constexpr std::string_view name = #memberName;                                       \
        static constexpr void set(auto& obj, type&& member) { obj.memberName = std::move(member); } \
        static constexpr const type& get(const auto& obj) { return obj.memberName; }                \
    };                                                                                              \
    decltype(_json_##memberName##_stub()) memberName

#define JSON_MEMBER(name) JSON_MEMBER_IMPL(name, __COUNTER__)

namespace json {
struct Null {};
template<typename T>
struct Nullable {
    std::optional<T> value;
};
struct RawDataView {
    std::string_view data;
};
struct RawStringView {
    std::string_view data;

    constexpr RawStringView()
        : data() { }
    constexpr RawStringView(std::string_view data)
        : data(data) { }
    constexpr RawStringView(const char* data)
        : data(data) { }
};
struct IntOrRawStringView {
    const char* begin = nullptr;
    int32_t size = 0;

    IntOrRawStringView()
        : begin(""), size(0) { }

    IntOrRawStringView(RawStringView view)
        : begin(view.data.begin()), size(view.data.length()) {
        if (begin == nullptr) {
            VERIFY(size == 0);
            begin = "";
        }
    }
    explicit IntOrRawStringView(int32_t i)
        : begin(nullptr), size(i) { }

    bool isString() const { return begin != nullptr; }
    bool isInt() const { return !isString(); }
    int32_t getInt() const {
        VERIFY(isInt());
        return size;
    }
    RawStringView string() const {
        VERIFY(isString());
        return { std::string_view(begin, size) };
    }
};

// https://www.crockford.com/mckeeman.html
struct Parser {
    Parser(const char* position)
        : position(position) { skipWhitespace(); }

    void skipWhitespace() {
        while (*position == ' ' || *position == '\n' || *position == '\r' || *position == '\t')
            position += 1;
    }
    void consume(char c) {
        VERIFY(*position == c);
        position += 1;
        skipWhitespace();
    }
    bool tryConsume(char c) {
        if (*position == c) {
            position += 1;
            skipWhitespace();
            return true;
        }
        return false;
    }

    bool tryConsumeNull();
    bool consumeBool();
    int32_t consumeInteger();
    std::string consumeString();
    RawStringView consumeStringRaw();
    RawDataView consumeValueRaw();

    const char* position;
    std::vector<char> scopes; // List of '{' and '[' characters
};

struct Formatter {

    void emit(char c) { output += c; }
    void emitRawData(RawDataView data) { output += data.data; }
    void emitRawString(RawStringView str) {
        emit('"');
        output += str.data;
        emit('"');
    }

    void formatNull() { output += "null"; }
    void formatBool(bool value) {
        output += value ? "true" : "false";
    }
    void formatInteger(int32_t);
    void formatString(std::string_view);

    std::string output;
};

template<typename T>
struct Impl;

template<typename T>
concept HasMemberFormatOverwrite = requires(const T& t) { { Impl<T>::shouldFormatMember(t) } -> std::same_as<bool>; };

template<typename T>
constexpr bool shouldFormatMember(const T& t) {
    if constexpr (HasMemberFormatOverwrite<T>)
        return Impl<T>::shouldFormatMember(t);
    else
        return true;
}

template<typename T>
inline T parse(RawDataView data) {
    Parser parser(data.data.data()); // TODO: Use string_view for parsing
    return Impl<T>::parse(parser);
}

template<typename T>
inline T parse(std::string_view data) {
    Parser parser(data.data()); // TODO: Use string_view for parsing
    return Impl<T>::parse(parser);
}

template<typename T>
inline std::string format(const T& t) {
    Formatter formatter;
    Impl<T>::format(formatter, t);
    return std::move(formatter.output);
}

template<>
struct Impl<bool> {
    static bool parse(Parser& parser) {
        return parser.consumeBool();
    }

    static void format(Formatter& formatter, bool value) {
        formatter.formatBool(value);
    }
};

template<>
struct Impl<int32_t> {
    static int32_t parse(Parser& parser) {
        return parser.consumeInteger();
    }

    static void format(Formatter& formatter, int32_t value) {
        formatter.formatInteger(value);
    }
};

template<typename T>
    requires std::is_enum_v<T>
struct Impl<T> {
    static_assert(std::is_same_v<std::underlying_type_t<T>, int32_t>);
    static T parse(Parser& parser) {
        return T(parser.consumeInteger());
    }

    static void format(Formatter& formatter, T value) {
        formatter.formatInteger(std::to_underlying(value));
    }
};

template<>
struct Impl<RawDataView> {
    static RawDataView parse(Parser& parser) {
        return { parser.consumeValueRaw() };
    }

    static void format(Formatter& formatter, RawDataView data) {
        formatter.emitRawData(data);
    }
};

template<>
struct Impl<RawStringView> {
    static RawStringView parse(Parser& parser) {
        return { parser.consumeStringRaw() };
    }

    static void format(Formatter& formatter, RawStringView data) {
        formatter.emitRawString(data);
    }
};

template<>
struct Impl<std::string> {
    static std::string parse(Parser& parser) {
        return parser.consumeString();
    }

    static void format(Formatter& formatter, std::string_view data) {
        formatter.formatString(data);
    }
};

template<>
struct Impl<IntOrRawStringView> {
    static IntOrRawStringView parse(Parser& parser) {
        if (*parser.position == '"')
            return parser.consumeStringRaw();
        else
            return IntOrRawStringView(parser.consumeInteger());
    }

    static void format(Formatter& formatter, IntOrRawStringView value) {
        if (value.isString())
            formatter.emitRawString(value.string());
        else
            formatter.formatInteger(value.getInt());
    }
};

template<typename T>
struct Impl<std::vector<T>> {
    static std::vector<T> parse(Parser& parser) {
        parser.consume('[');
        if (parser.tryConsume(']'))
            return {};

        std::vector<T> result;
        for (;;) {
            result.emplace_back(Impl<T>::parse(parser));
            if (!parser.tryConsume(','))
                break;
        }
        parser.consume(']');
        return result;
    }

    static void format(Formatter& formatter, const std::vector<T>& array) {
        formatter.emit('[');
        bool listEmpty = true;
        for (const T& t : array) {
            if (!listEmpty)
                formatter.emit(',');
            else
                listEmpty = false;
            Impl<T>::format(formatter, t);
        }
        formatter.emit(']');
    }
};

template<typename T>
struct Impl<std::optional<T>> {
    static T parse(Parser& parser) { return Impl<T>::parse(parser); }
    static void format(Formatter& formatter, const std::optional<T>& opt) {
        VERIFY(opt.has_value()); // Cannot format empty optional
        Impl<T>::format(formatter, opt.value());
    }
    static bool shouldFormatMember(const std::optional<T>& opt) { return opt.has_value(); }
};

template<>
struct Impl<Null> {
    static Null parse(Parser& parser) {
        VERIFY(parser.tryConsumeNull());
        return {};
    }
    static void format(Formatter& formatter, Null) {
        formatter.formatNull();
    }
};

template<typename T>
struct Impl<Nullable<T>> {
    static Nullable<T> parse(Parser& parser) {
        if (parser.tryConsumeNull())
            return Nullable<T> { std::nullopt };

        return { Impl<T>::parse(parser) };
    }

    static void format(Formatter& formatter, const Nullable<T>& opt) {
        if (opt.value.has_value())
            Impl<T>::format(formatter, opt.value.value());
        else
            formatter.formatNull();
    }
};

}