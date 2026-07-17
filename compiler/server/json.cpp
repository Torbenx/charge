#include <server/json_objects.h>
#include <server/json_tuple.h>

#include <gtest/gtest.h>

namespace json {

bool Parser::tryConsumeNull() {
    if (position[0] == 'n' && position[1] == 'u' && position[2] == 'l' && position[3] == 'l') {
        position += 4;
        skipWhitespace();
        return true;
    }
    return false;
}

bool Parser::consumeBool() {
    if (position[0] == 't' && position[1] == 'r' && position[2] == 'u' && position[3] == 'e') {
        position += 4;
        skipWhitespace();
        return true;
    } else if (position[0] == 'f' && position[1] == 'a' && position[2] == 'l' && position[3] == 's' && position[4] == 'e') {
        position += 5;
        skipWhitespace();
        return false;
    } else
        VERIFY_NOT_REACHED();
}

int32_t Parser::consumeInteger() {
    bool negate = tryConsume('-');
    int64_t value = 0;
    while (*position >= '0' && *position <= '9') {
        int64_t curDig = *position - '0';
        position += 1;
        value = value * 10 + curDig;
    }
    skipWhitespace();
    if (negate) {
        VERIFY(-value >= (int32_t)limits::min);
        return -value;
    } else {
        VERIFY(value <= (int32_t)limits::max);
        return value;
    }
}

RawStringView Parser::consumeStringRaw() {
    VERIFY(*position == '"');
    position += 1;
    const char* begin = position;
    for (;;) {
        if (*position == '"') {
            const char* testPosition = position - 1;
            bool isEscaped = false;
            while (*testPosition == '\\') {
                isEscaped = !isEscaped;
                testPosition -= 1;
            }
            if (!isEscaped)
                break;
        }
        position += 1;
    }
    const char* end = position;
    consume('"');
    skipWhitespace();
    return { std::string_view { begin, end } };
}

std::string Parser::consumeString() {
    VERIFY(*position == '"');
    position += 1;
    std::string out;
    for (; *position != '"'; ++position) {
        char c = *position;
        if (c == '\\') {
            position += 1;
            char control = *position;
            if (control == '\\') {
                out.push_back('\\');
            } else if (control == '"') {
                out.push_back('"');
            } else if (control == 'n') {
                out.push_back('\n');
            } else if (control == 'r') {
                out.push_back('\r');
            } else if (control == 't') {
                out.push_back('\t');
            } else if (control == '/') {
                out.push_back('/');
            } else if (control == 'b') {
                out.push_back('\b');
            } else if (control == 'f') {
                out.push_back('\f');
            } else if (control == 'u') {
                // TODO: Needs bounds check
                uint16_t val = hex_value16({ { position[1], position[2], position[3], position[4] } });
                position += 4;
                VERIFY(val < 128); // TODO: Support encoding as utf-8
                out.push_back(val);
            }
        } else {
            out.push_back(c);
        }
    }
    consume('"');
    skipWhitespace();
    return out;
}

RawDataView Parser::consumeValueRaw() {
    const char* begin = position;
    switch (*position) {
    case '"':
        consumeStringRaw();
        break;
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        consumeInteger();
        break;
    case 't':
    case 'f':
        consumeBool();
        break;
    case 'n':
        VERIFY(tryConsumeNull());
        break;
    case '{':
    case '[':
        VERIFY(scopes.empty());
        scopes.push_back(*position);
        position += 1;
        while (!scopes.empty()) {
            if (*position == '"') {
                consumeStringRaw();
                continue;
            }
            if (*position == '{' || *position == '[') {
                scopes.push_back(*position);
            } else if (*position == '}' || *position == ']') {
                VERIFY(scopes.back() == *position - 2);
                scopes.pop_back();
            }
            position += 1;
        }
        break;
    default:
        VERIFY_NOT_REACHED();
    }
    skipWhitespace();
    return { std::string_view { begin, position } };
}

void Formatter::formatInteger(int32_t value) {
    output += std::to_string(value);
}

void Formatter::formatString(std::string_view in) {
    output.push_back('"');
    for (char c : in) {
        if (c < ' ') {
            if (c == '\n') {
                output += "\\n";
            } else if (c == '\r') {
                output += "\\r";
            } else if (c == '\t') {
                output += "\\t";
            } else {
                output += "\\u00";
                output += hex_string8(c);
            }
        } else if (c == '"') {
            output += "\\\"";
        } else if (c == '\\') {
            output += "\\\\";
        } else {
            output.push_back(c);
        }
    }
    output.push_back('"');
}

}

namespace json::object_detail {

TEST(JsonObject, EntropyBitRange) {
    std::array<uint8_t, 4> data;
    data = { 0b0101, 0b0100, 0b0000, 0b0000 };
    auto result = findMaxEntropyBitRange(data, 1);
    EXPECT_EQ(result.length, 1);
    EXPECT_EQ(result.startIndex, 2);
    EXPECT_DOUBLE_EQ(result.entropy, 1.0);
}

TEST(JsonObject, HashSolution) {
    std::vector<std::vector<uint8_t>> data;
    data.push_back({ 0b101, 0b101 });
    data.push_back({ 0b111, 0b001 });
    auto sol = findSolution(data, 2);
    ASSERT_TRUE(sol.has_value());
    EXPECT_EQ(sol->bitRangeLength, 1);
    EXPECT_EQ(sol->primeModulo, 2);
    ASSERT_EQ(sol->usedDataCount, 1);
    EXPECT_EQ(sol->usedData[0].factor, 1);
    EXPECT_EQ(sol->usedData[0].dataIndex, 1);
    EXPECT_EQ(sol->usedData[0].bitRangeStart, 1);
}

TEST(JsonObject, SillyLog2) {
    for (double testValue : { 1.0, 0.5, 0.25, 0.125, 0.1, 0.00001, 0.9999 })
        EXPECT_NEAR(sillyLog2(testValue), std::log2(testValue), 1e-16) << "Failure for log2(" << testValue << ")";
}

}

struct TestObj {
    JSON_OBJECT
    int32_t JSON_MEMBER(a) = 0;
    int32_t JSON_MEMBER(b) = 0;
};
TEST(Json, ParseObject) {
    TestObj result = json::parse<TestObj>(R"({ "a": 1, "b": 2 })");
    EXPECT_EQ(result.a, 1);
    EXPECT_EQ(result.b, 2);
}
TEST(Json, FormatObject) {
    TestObj obj { 1, 2 };
    EXPECT_EQ(json::format(obj), R"({"a":1,"b":2})");
}

struct TestListObj {
    JSON_OBJECT
    int32_t JSON_MEMBER(c) = 0;
    std::vector<TestObj> JSON_MEMBER(list);
};
TEST(Json, ParseList) {
    TestListObj result = json::parse<TestListObj>(R"({ "list": [{ "a": 1, "b": 2 }, { "b": 3, "a": 4 }], "c": 5 })");
    EXPECT_EQ(result.c, 5);
    ASSERT_EQ(result.list.size(), 2);
    EXPECT_EQ(result.list[0].a, 1);
    EXPECT_EQ(result.list[0].b, 2);
    EXPECT_EQ(result.list[1].a, 4);
    EXPECT_EQ(result.list[1].b, 3);
}
TEST(Json, FormatList) {
    TestListObj obj;
    obj.c = 5;
    obj.list.push_back({ 1, 2 });
    obj.list.push_back({ 3, 4 });
    EXPECT_EQ(json::format(obj), R"({"c":5,"list":[{"a":1,"b":2},{"a":3,"b":4}]})");
}

TEST(Json, ParseIngoreUnkownMembers) {
    TestObj result = json::parse<TestObj>(R"({ "a": 1, "ignore1": 1, "ignore2": [], "ignore3": { "str": "blub" }, "b": 2 })");
    EXPECT_EQ(result.a, 1);
    EXPECT_EQ(result.b, 2);
}

struct TestNullableObj {
    JSON_OBJECT
    json::Nullable<TestObj> JSON_MEMBER(obj);
};
TEST(Json, ParseNullable) {
    auto resultNull = json::parse<TestNullableObj>(R"({ "obj": null })");
    EXPECT_FALSE(resultNull.obj.value.has_value());

    auto resultValue = json::parse<TestNullableObj>(R"({ "obj": { "a": 1, "b": 2 } })");
    ASSERT_TRUE(resultValue.obj.value.has_value());
    EXPECT_EQ(resultValue.obj.value->a, 1);
    EXPECT_EQ(resultValue.obj.value->b, 2);
}
TEST(Json, FormatNullable) {
    TestNullableObj obj;
    EXPECT_EQ(json::format(obj), R"({"obj":null})");

    obj.obj.value = TestObj { 1, 2 };
    EXPECT_EQ(json::format(obj), R"({"obj":{"a":1,"b":2}})");
}

struct TestOptionalObj {
    JSON_OBJECT
    std::optional<TestObj> JSON_MEMBER(obj);
    int32_t JSON_MEMBER(c) = 0;
};
TEST(Json, FormatOptional) {
    TestOptionalObj obj;
    obj.c = 3;
    EXPECT_EQ(json::format(obj), R"({"c":3})");

    obj.obj = TestObj { 1, 2 };
    EXPECT_EQ(json::format(obj), R"({"obj":{"a":1,"b":2},"c":3})");
}

using TestTuple = json::Tuple<json::Types<int32_t, TestObj>, json::Names<"c", "obj">>;
TEST(Json, ParseTuple) {
    auto result = json::parse<TestTuple>(R"({ "obj": { "a": 1, "b": 2 }, "c": 3 })");
    EXPECT_EQ(result.get<"obj">().a, 1);
    EXPECT_EQ(result.get<"obj">().b, 2);
    EXPECT_EQ(result.get<"c">(), 3);
}
TEST(Json, FormatTuple) {
    TestTuple tuple;
    tuple.get<"obj">() = { .a = 1, .b = 2 };
    tuple.get<"c">() = 3;
    EXPECT_EQ(json::format(tuple), R"({"c":3,"obj":{"a":1,"b":2}})");
}