#pragma once

#include <server/json.h>

namespace json {

template<int_t N>
struct FixedString {
    std::array<char, N + 1> storage;
    constexpr FixedString(const char (&str)[N + 1]) {
        std::copy_n(str, N + 1, storage.begin());
        VERIFY(str[N] == '\0');
    }
    constexpr operator std::string_view() const { return { storage.begin(), N }; }
};
template<int_t N>
FixedString(const char (&)[N]) -> FixedString<N - 1>;

template<typename... Ts>
struct Types { };
template<FixedString... Ns>
struct Names { };

namespace tuple_detail {

    template<typename T, FixedString N, int_t I>
    struct Storage {
        struct ref_data {
            using type = T;
            static constexpr std::string_view name = N;
            static constexpr void set(auto& tuple, T&& value) {
                tuple.template get<I>() = value;
            }
            static constexpr const T& get(const auto& tuple) {
                return tuple.template get<I>();
            }
        };

        T value = {};
    };

    template<typename, typename, typename>
    struct Container;

    template<typename T, FixedString N, int_t I>
    auto& by_type(Storage<T, N, I>& storage) { return storage; }
    template<typename T, FixedString N, int_t I>
    const auto& by_type(const Storage<T, N, I>& storage) { return storage; }

    template<FixedString N, typename T, int_t I>
    auto& by_name(Storage<T, N, I>& storage) { return storage; }
    template<FixedString N, typename T, int_t I>
    const auto& by_name(const Storage<T, N, I>& storage) { return storage; }

    template<int_t I, typename T, FixedString N>
    auto& by_index(Storage<T, N, I>& storage) { return storage; }
    template<int_t I, typename T, FixedString N>
    const auto& by_index(const Storage<T, N, I>& storage) { return storage; }

    template<typename... Ts, FixedString... Ns, int_t... Is>
    struct Container<Types<Ts...>, Names<Ns...>, int_sequence<Is...>> : Storage<Ts, Ns, Is>... { };
}

template<typename, typename>
struct Tuple;
template<typename... Ts, FixedString... Ns>
struct Tuple<Types<Ts...>, Names<Ns...>> {
    static_assert(sizeof...(Ts) == sizeof...(Ns));

    template<typename T>
    auto& get() { return tuple_detail::by_type<T>(container).value; }
    template<typename T>
    const auto& get() const { return tuple_detail::by_type<T>(container).value; }

    template<FixedString N>
    auto& get() { return tuple_detail::by_name<N>(container).value; }
    template<FixedString N>
    const auto& get() const { return tuple_detail::by_name<N>(container).value; }

    template<int_t I>
    auto& get() { return tuple_detail::by_index<I>(container).value; }
    template<int_t I>
    const auto& get() const { return tuple_detail::by_index<I>(container).value; }

private:
    using Container = tuple_detail::Container<Types<Ts...>, Names<Ns...>, int_sequence_for<Ts...>>;
    Container container;

public:
    static constexpr int_t _json_base_counter = 0; // Tag to be recognized as a json object
    template<int_t I>
        requires(I < sizeof...(Ts))
    using _json_ref_data = typename std::decay_t<decltype(tuple_detail::by_index<I>(Container()))>::ref_data;
};

}