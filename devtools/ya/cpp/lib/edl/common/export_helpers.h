#pragma once

#include "members.h"

#include <library/cpp/json/writer/json_value.h>

#include <util/generic/maybe.h>
#include <util/generic/ptr.h>

#include <utility>

namespace NYa::NEdl {

    template <class E, class T>
    struct TExportHelper {
        static void Export(E&& e, const T& val);
    };

    template <class E, class T>
    inline void Export(E&& e, const T& val) {
        TExportHelper<E, T>::Export(std::forward<E>(e), val);
    }

    template <class E, class T>
    requires std::is_enum_v<T>
    struct TExportHelper<E, T> {
        static void Export(E&& e, const T& val) {
            e.ExportValue(ToString(val));
        }
    };

    template <class T>
    constexpr bool IsOptional = false;

    template <class T>
    constexpr bool IsOptional<TMaybe<T>> = true;

    template <class T>
    constexpr bool IsOptional<THolder<T>> = true;

    template <class T>
    constexpr bool IsOptional<TIntrusivePtr<T>> = true;

    template <class T>
    concept COptional = IsOptional<T>;

    template <class E, COptional T>
    struct TExportHelper<E, T> {
        static void Export(E&& e, const T& val) {
            if (val) {
                e.ExportValue(*val);
            } else {
                e.ExportNullValue();
            }
        }
    };

    template <class E, class T>
    void ExportRange(E&& e, const T& r) {
        e.ExportRange(std::begin(r), std::end(r));
    }

    template <class E, class T, class A>
    struct TExportHelper<E, TVector<T, A>> {
        static void Export(E&& e, const TVector<T, A>& vec) {
            ExportRange(std::forward<E>(e), vec);
        }
    };

    template <class E, class T, size_t N>
    struct TExportHelper<E, std::array<T, N>> {
        static void Export(E&& e, const std::array<T, N>& arr) {
            ExportRange(std::forward<E>(e), arr);
        }
    };

    template <class E, class T>
    void ExportMap(E&& e, const T& map) {
        e.OpenMap();
        for (auto&& [k, v] : map) {
            e.AddMapItem(k, v);
        }
        e.CloseMap();
    }

    template <class E, class K, class V, class H, class Eq, class A>
    struct TExportHelper<E, THashMap<K, V, H, Eq, A>> {
        static void Export(E&& e, const THashMap<K, V, H, Eq, A>& map) {
            ExportMap(std::forward<E>(e), map);
        }
    };

    template <class E>
    struct TExportHelper<E, NJson::TJsonValue> {
        static void Export(E&& e, const NJson::TJsonValue& val) {
            switch (val.GetType()) {
                case NJson::JSON_NULL:
                    e.ExportNullValue();
                    break;
                case NJson::JSON_BOOLEAN:
                    e.ExportValue(val.GetBooleanSafe());
                    break;
                case NJson::JSON_INTEGER:
                    e.ExportValue(val.GetIntegerSafe());
                    break;
                case NJson::JSON_UINTEGER:
                    e.ExportValue(val.GetUIntegerSafe());
                    break;
                case NJson::JSON_DOUBLE:
                    e.ExportValue(val.GetDoubleSafe());
                    break;
                case NJson::JSON_STRING:
                    e.ExportValue(val.GetStringSafe());
                    break;
                case NJson::JSON_ARRAY:
                    ExportRange(std::forward<E>(e), val.GetArraySafe());
                    break;
                case NJson::JSON_MAP:
                    ExportMap(std::forward<E>(e), val.GetMapSafe());
                    break;
                case NJson::JSON_UNDEFINED:
                    ythrow yexception() << "Uninitialized json value";
            }
        }
    };

    template <class E>
    struct TExportHelper<E, std::monostate> {
        static void Export(E&& e, const std::monostate&) {
            e.ExportNullValue();
        }
    };

    template <class E, class... Args>
    struct TExportHelper<E, std::variant<Args...>> {
        static void Export(E&& e, const std::variant<Args...>& val) {
            std::visit(
                [&](auto&& arg) {e.ExportValue(arg);},
                val
            );
        }
    };

    template <class T>
    struct TEmptyChecker {
        static bool IsEmpty(const T&) {
            return false;
        }
    };

    template <class T>
    concept CSupportsStdIsEmpty = requires(T val) {
        std::empty(val);
        requires !std::is_constructible_v<bool, T>;
    };

    template <CSupportsStdIsEmpty T>
    struct TEmptyChecker<T> {
        static bool IsEmpty(const T& val) {
            return std::empty(val);
        }
    };

    template <class T>
    requires std::is_constructible_v<bool, T>
    struct TEmptyChecker<T> {
        static bool IsEmpty(const T& val) {
            return !bool(val);
        }
    };

    template <class T>
    bool IsEmpty(const T& val) {
        return TEmptyChecker<T>::IsEmpty(val);
    }

    template <class... Args>
    struct TEmptyChecker<std::variant<Args...>> {
        static bool IsEmpty(const std::variant<Args...>& val) {
            return std::visit(
                [](auto&& v) {return NYa::NEdl::IsEmpty(v);},
                val
            );
        }
    };

    template <class E, class T, size_t I>
    void AddStructMember(E&& e, const T& val) {
        using MemberInfo = typename T::template EdlMemberInfo<I>;
        const auto& m = MemberInfo::GetRef(val);
        if (MemberInfo::ExportPolicy == NEdl::EMemberExportPolicy::ALWAYS || !IsEmpty(m)) {
            e.AddMapItem(MemberInfo::GetExportName(), m);
        }
    }

    template <class E, class T, size_t... Is>
    void ExportStructMembers(E&& e, const T& val, std::index_sequence<Is...>) {
        (
            AddStructMember<E, T, Is>(std::forward<E>(e), val),
            ...
        );
    }

    template <class E, class T>
    concept CHasExportMembers = requires(E e, T val) {
        val.ExportMembers(std::forward<E>(e));
    };

    template <class E, CHasEdlMemberInfo T>
    struct TExportHelper<E, T> {
        static void Export(E&& e, const T& val) {
            e.OpenMap();
            ExportStructMembers(std::forward<E>(e), val, std::make_index_sequence<T::EdlMemberCount>());
            if constexpr (CHasEdlDefaultMember<T>) {
                e.ExportInnerValue(T::template GetDefaultMemberRef(val));
            }
            if constexpr (CHasExportMembers<E, T>) {
                val.ExportMembers(std::forward<E>(e));
            }
            e.CloseMap();
        }
    };
}
