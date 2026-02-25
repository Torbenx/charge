#pragma once

#include <server/json.h>

namespace json::object_detail {

template<typename T, int_t I>
using RefData = typename T::template _json_ref_data<I - T::_json_base_counter>;

template<typename T>
concept RefObject = T::_json_base_counter >= 0;

constexpr double sillyLog2(double v) {
    if (v == 0.0)
        return -std::numeric_limits<double>::infinity();

    // Input is assumned to be with in [0, 1]
    int_t intPart = 0;
    while (v < 1.0) {
        intPart -= 1;
        v *= 2.0;
    }

    double fracPart = 0.0;
    double mult = 1.0;
    for (;;) {
        VERIFY(1.0 <= v && v < 2.0);
        if (v == 1.0)
            break;
        while (v < 2.0) {
            v = v * v;
            mult *= 0.5;
        }
        double newFracPart = fracPart + mult;
        if (newFracPart == fracPart)
            break;
        fracPart = newFracPart;
        v *= 0.5;
    }
    return (double)intPart + fracPart;
}

constexpr double constexprLog2(double v) {
    if consteval {
        return sillyLog2(v);
    } else {
        return std::log2(v);
    }
}

struct EntropyBitRange {
    int_t startIndex;
    int_t length;
    double entropy;
};
constexpr EntropyBitRange findMaxEntropyBitRange(std::span<const uint8_t> data, int_t rangeLength) {
    VERIFY(rangeLength <= 8);
    std::array<double, 8> entropies {};
    for (int_t i = 0; i < 8; i++) {
        int_t oneCount = 0;
        for (auto d : data)
            oneCount += (d >> i) & 1u;
        double oneProb = (double)oneCount / (double)data.size();
        if (oneProb == 0.0 || oneProb == 1.0)
            entropies[i] = 0.0;
        else
            entropies[i] = -oneProb * constexprLog2(oneProb) - (1.0 - oneProb) * constexprLog2(1.0 - oneProb);
    }
    double bestSum = 0.0;
    int_t bestStartIndex = 0;
    for (int_t startIndex = 0; startIndex <= 8 - rangeLength; startIndex++) {
        double sum = 0.0;
        for (int_t i = 0; i < rangeLength; i++)
            sum += entropies[startIndex + i];
        if (sum > bestSum) {
            bestSum = sum;
            bestStartIndex = startIndex;
        }
    }
    return { bestStartIndex, rangeLength, bestSum };
}

struct BitRangeAndIndex {
    EntropyBitRange range;
    int_t dataIndex;
};
constexpr std::vector<BitRangeAndIndex> rankData(const std::vector<std::vector<uint8_t>>& dataVectors, int_t bitRangeLength) {
    std::vector<BitRangeAndIndex> result;
    for (int_t dataIndex = 0; dataIndex < (int_t)dataVectors.size(); dataIndex++) {
        auto range = findMaxEntropyBitRange(dataVectors[dataIndex], bitRangeLength);
        if (range.entropy > 0.0)
            result.push_back({ range, dataIndex });
    }
    // TODO: This should use std::stable_sort, but that isn't constexpr
    std::ranges::sort(result, [](const BitRangeAndIndex& a, const BitRangeAndIndex& b) constexpr {
        auto eOrdering = a.range.entropy <=> b.range.entropy;
        if (eOrdering != std::partial_ordering::equivalent)
            return eOrdering > 0;
        // Sort by dataIndex to make this a stable sort.
        return a.dataIndex < b.dataIndex;
    });
    return result;
}

constexpr bool increment(std::vector<uint8_t>& vals, uint8_t p) {
    int_t i = 0;
    for (; i < (int_t)vals.size(); i++) {
        vals[i] += 1;
        if (vals[i] == p)
            vals[i] = 0;
        else
            break;
    }
    return i == (int_t)vals.size();
}

struct Solution {
    static constexpr int_t MAX_DATA_USES = 10;

    struct UsedData {
        int_t dataIndex = 0;
        int_t bitRangeStart = 0;
        uint8_t factor = 0;
    };

    int_t primeModulo;
    int_t bitRangeLength;
    int_t usedDataCount;
    std::array<UsedData, MAX_DATA_USES> usedData;
};

constexpr std::optional<Solution> findSolution(const std::vector<std::vector<uint8_t>>& dataVectors, int_t p) {
    auto bits = std::min<size_t>(8, std::bit_ceil<size_t>(p - 1));
    uint8_t bitsMask = ((size_t)1 << bits) - 1u;
    int_t inputCount = dataVectors.front().size();
    if (inputCount > p)
        return std::nullopt;
    auto ranking = rankData(dataVectors, bits);
    VERIFY(!ranking.empty());

    std::vector<std::vector<uint8_t>> inputVectors;
    for (int_t i = 0; i < (int_t)ranking.size(); i++) {
        const auto& [range, dataIndex] = ranking[i];
        std::vector<uint8_t> input;
        for (uint8_t d : dataVectors[dataIndex])
            input.push_back((d >> range.startIndex) & bitsMask);
        inputVectors.emplace_back(std::move(input));
    }

    std::vector<uint8_t> state;
    state.resize(ranking.size() - 1);
    std::vector<size_t> results;
    results.resize(inputCount);
    for (;;) {
        for (int_t j = 0; j < inputCount; j++)
            results[j] = inputVectors[0][j];
        for (int_t i = 1; i < (int_t)ranking.size(); i++) {
            for (int_t j = 0; j < inputCount; j++)
                results[j] += (size_t)inputVectors[i][j] * (size_t)state[i - 1];
        }
        for (int_t j = 0; j < inputCount; j++)
            results[j] %= (size_t)p;
        std::ranges::sort(results);
        if (std::ranges::adjacent_find(results) == results.end())
            break;
        if (increment(state, p))
            return std::nullopt;
    }

    std::array<Solution::UsedData, Solution::MAX_DATA_USES> usedData {};
    int_t outputIndex = 0;
    for (int_t i = 0; i < (int_t)ranking.size(); i++) {
        uint8_t factor = i == 0 ? 1 : state[i - 1];
        if (factor != 0)
            usedData[outputIndex++] = { ranking[i].dataIndex, ranking[i].range.startIndex, factor };
    }
    return Solution { p, (int_t)bits, outputIndex, usedData };
}

constexpr Solution findSolution(const std::vector<std::vector<uint8_t>>& dataVectors) {
    int_t inputCount = dataVectors.front().size();
    if (inputCount <= 1)
        return Solution { 1, 0, 0, {} };

    for (int_t p : { 2, 3, 5, 7, 11 }) {
        auto sol = findSolution(dataVectors, p);
        if (sol.has_value())
            return sol.value();
    }
    VERIFY_NOT_REACHED();
}

template<typename T, int_t I>
struct InvokeIfMember {
    template<typename F>
    static constexpr void invoke(const F&) { }
};
template<typename T, int_t I>
    requires requires { typename RefData<T, I>::type; }
struct InvokeIfMember<T, I> {
    template<typename F>
    static constexpr void invoke(const F& f) { f(std::type_identity<RefData<T, I>>()); }
};

template<typename T, typename F, int_t... I>
constexpr void forEachMemberHelper(const F& f, int_sequence<I...>) {
    (InvokeIfMember<T, I>::template invoke<F>(f), ...);
}

template<RefObject T, typename F>
constexpr void forEachMember(const F& f) {
    forEachMemberHelper<T, F>(f, make_int_sequence<100>());
}

template<typename T>
consteval int_t memberCount() {
    int_t counter = 0;
    forEachMember<T>([&counter](auto) { counter += 1; });
    return counter;
}

template<typename T>
using MemberParseFunction = void (*)(Parser&, T&);

template<typename T>
struct MemberInfo {
    std::string_view name = {};
    MemberParseFunction<T> parseFunc = nullptr;
};

template<typename T>
consteval auto collectMemberInfos() {
    std::array<MemberInfo<T>, memberCount<T>()> result;
    int_t counter = 0;
    forEachMember<T>([&result, &counter]<typename RefData>(std::type_identity<RefData>) constexpr {
        result[counter++] = {
            .name = RefData::name,
            .parseFunc = [](Parser& parser, T& parent) constexpr {
                RefData::set(parent, Impl<typename RefData::type>::parse(parser));
            }
        };
    });
    return result;
}

constexpr auto toDataVectors(const auto& members) {
    std::vector<std::vector<uint8_t>> result;
    result.emplace_back();
    for (const auto& member : members)
        result.back().push_back(member.name.length());
    for (int_t i = 0; i < 10; i++) {
        result.emplace_back();
        for (const auto& member : members)
            result.back().push_back(i < (int_t)member.name.length() ? member.name[i] : 0);
    }
    return result;
}

constexpr uint8_t getData(std::string_view name, int_t dataIndex) {
    if (dataIndex == 0)
        return name.length();
    if (dataIndex - 1 < (int_t)name.length())
        return name[dataIndex - 1];
    return 0;
}

constexpr uint32_t dynamicEvaluateHash(const Solution& sol, std::string_view name) {
    uint32_t mask = (1u << sol.bitRangeLength) - 1u;
    uint32_t result = 0;
    for (int_t i = 0; i < sol.usedDataCount; i++) {
        const auto& usage = sol.usedData[i];
        uint8_t data = getData(name, usage.dataIndex);
        data = (data >> usage.bitRangeStart) & mask;
        result += (uint32_t)data * (uint32_t)usage.factor;
    }
    return result % (uint32_t)sol.primeModulo;
}

template<Solution sol, int_t... I>
constexpr uint32_t staticEvaluateHashHelper(std::string_view name, int_sequence<I...>) {
    static constexpr uint32_t mask = (1u << sol.bitRangeLength) - 1u;
    return ((((getData(name, sol.usedData[I].dataIndex) >> sol.usedData[I].bitRangeStart) & mask) * sol.usedData[I].factor) + ... + 0) % (uint32_t)sol.primeModulo;
}
template<Solution sol>
constexpr uint32_t staticEvaluateHash(std::string_view name) {
    return staticEvaluateHashHelper<sol>(name, make_int_sequence<sol.usedDataCount>());
}

template<int_t tableSize, typename T>
constexpr auto buildJumpTable(const Solution& solution, const T& infos) {
    std::array<typename T::value_type, tableSize> result {};
    for (const auto& member : infos) {
        uint32_t idx = dynamicEvaluateHash(solution, member.name);
        VERIFY(result[idx].name.empty());
        result[idx] = member;
    }
    return result;
}
}

namespace json {

template<object_detail::RefObject T>
struct Impl<T> {
    static T parse(Parser& parser) {
        using namespace object_detail;
        static constexpr auto memberInfos = collectMemberInfos<T>();
        static constexpr Solution solution = findSolution(toDataVectors(memberInfos));
        static constexpr auto jumpTable = buildJumpTable<solution.primeModulo>(solution, memberInfos);

        T result {};
        parser.consume('{');
        if (parser.tryConsume('}'))
            return result;

        for (;;) {
            std::string_view name = parser.consumeStringRaw().data;
            parser.consume(':');
            const auto& target = jumpTable[staticEvaluateHash<solution>(name)];
            if (target.name == name)
                target.parseFunc(parser, result);
            else
                parser.consumeValueRaw();
            if (!parser.tryConsume(','))
                break;
        }
        parser.consume('}');
        return result;
    }

    static void format(Formatter& formatter, const T& value) {
        formatter.emit('{');
        bool isOutputEmpty = true;
        object_detail::forEachMember<T>([&formatter, &value, &isOutputEmpty]<typename RefData>(std::type_identity<RefData>) {
            const auto& member = RefData::get(value);
            if (shouldFormatMember(member)) {
                if (isOutputEmpty)
                    isOutputEmpty = false;
                else
                    formatter.emit(',');
                formatter.emitRawString({ RefData::name });
                formatter.emit(':');
                Impl<typename RefData::type>::format(formatter, member);
            }
        });
        formatter.emit('}');
    }
};

}