#pragma once

#include "members.h"

#include <library/cpp/json/writer/json_value.h>

#include <util/generic/ptr.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>
#include <util/stream/str.h>
#include <util/string/cast.h>
#include <util/system/type_name.h>

#include <array>
#include <limits>

namespace NYa::NEdl {
    struct TBaseLoader;
    using TLoaderPtr = THolder<TBaseLoader>;
    struct TLoaderError : yexception {

    };

    template <class T>
    class TLoader;

    template <class T>
    inline TLoaderPtr GetLoader(T& val) {
        return MakeHolder<TLoader<T>>(val);
    }

    struct TBaseLoader {
        virtual void SetValue(std::nullptr_t val);
        virtual void SetValue(bool val);
        virtual void SetValue(long long val);
        virtual void SetValue(unsigned long long val);
        virtual void SetValue(double val);
        virtual void SetValue(const TStringBuf val);
        virtual void EnsureMap();
        virtual void EnsureArray();
        virtual TLoaderPtr AddMapValue(TStringBuf key);
        virtual TLoaderPtr AddArrayValue();
        virtual void Finish();
        virtual ~TBaseLoader() = default;
    };

    // All loaded data is ignored
    // Useful to skip useless json subtrees
    struct TBlackHoleLoader : public TBaseLoader {
    public:
        void SetValue(std::nullptr_t) override;
        void SetValue(bool) override;
        void SetValue(long long) override;
        void SetValue(unsigned long long) override;
        void SetValue(double) override;
        void SetValue(const TStringBuf) override;
        void EnsureMap() override;
        TLoaderPtr AddMapValue(TStringBuf) override;
        void EnsureArray() override;
        TLoaderPtr AddArrayValue() override;
        void Finish() override;
    };

    struct TBlackHole {
    };

    template <>
    class TLoader<TBlackHole> : public TBlackHoleLoader {
    public:
        TLoader(TBlackHole&) {
        }
    };

    template <class T>
    class TLoaderForRef : public TBaseLoader {
    public:
        TLoaderForRef(T& val)
            : ValueRef_{val}
        {
        }

    protected:
        T& ValueRef_;
    };

    template <class T>
    class TStringValueLoader : public TLoaderForRef<T> {
    public:
        using TLoaderForRef<T>::TLoaderForRef;

        inline void SetValue(const TStringBuf val) override {
            this->ValueRef_ = val;
        }
    };

    template <>
    class TLoader<TString>: public TStringValueLoader<TString> {
        using TStringValueLoader<TString>::TStringValueLoader;
    };

    template <>
    class TLoader<double> : public TLoaderForRef<double> {
    public:
        using TLoaderForRef<double>::TLoaderForRef;

        inline void SetValue(long long val) override {
            this->ValueRef_ = val;
        }

        inline void SetValue(unsigned long long val) override {
            this->ValueRef_ = val;
        }

        inline void SetValue(double val) override {
            this->ValueRef_ = val;
        }
    };

    template <>
    class TLoader<bool> : public TLoaderForRef<bool> {
    public:
        using TLoaderForRef<bool>::TLoaderForRef;

        inline void SetValue(bool val) override {
            this->ValueRef_ = val;
        }
    };

    template <class T>
    concept CInteger = std::is_integral_v<T> && !std::is_same_v<T, bool>;

    template <CInteger T, CInteger V>
    requires (std::is_same_v<T, V>)
    inline void CheckIntegerBounds(V) {
    }

    template <CInteger T, CInteger V>
    requires (!std::is_same_v<T, V>)
    inline void CheckIntegerBounds(V val) {
        if constexpr (std::is_unsigned_v<V>) {
            // It is safe to cast max(T) to any unsigned V type
            if (val <= static_cast<V>(Max<T>())) {
                return;
            }
        } else {
            if constexpr (std::is_signed_v<T>) {
                if (val >= Min<T>() && val <= Max<T>()) {
                    return;
                }
            } else {
                if (val >= 0 && static_cast<std::make_unsigned_t<V>>(val) <= Max<T>()) {
                    return;
                }
            }
        }
        ythrow TLoaderError() << "value " << val << " is out of range for '" << TypeName(typeid(decltype(T()))) << "' type";
    }

    template <CInteger T>
    class TLoader<T> : public TLoaderForRef<T> {
    public:
        using TLoaderForRef<T>::TLoaderForRef;

        inline void SetValue(long long val) override {
            DoSetValue(val);
        }

        inline void SetValue(unsigned long long val) override {
            DoSetValue(val);
        }

    private:
        template <class V>
        void DoSetValue(V val) {
            CheckIntegerBounds<T, V>(val);
            this->ValueRef_ = val;
        }
    };

    // Now supports only string enum representation (See GENERATE_ENUM_SERIALIZATION() ya.make macro)
    // May cause linkage error "function 'FromStringImpl<XXX, char>' is used but not defined in this translation unit...".
    // In such case write TLoader specialization for your enum
    // TODO Add support for enums which can be saved and loaded as integers only
    template <class T>
    requires std::is_enum_v<T>
    class TLoader<T> : public TLoaderForRef<T> {
    public:
        using TLoaderForRef<T>::TLoaderForRef;

        inline void SetValue(const TStringBuf val) override {
            try {
                this->ValueRef_ = FromString<T>(val);
            } catch (yexception& e) {
                ythrow TLoaderError() << e.what();
            }
        }
    };

    template <class T, class A>
    class TLoader<TVector<T, A>> : public TLoaderForRef<TVector<T, A>> {
    public:
        using TLoaderForRef<TVector<T, A>>::TLoaderForRef;

        inline void EnsureArray() override {
        }

        TLoaderPtr AddArrayValue() override {
            this->ValueRef_.push_back({});
            return GetLoader(this->ValueRef_.back());
        }
    };

    template <class T, size_t N>
    class TLoader<std::array<T, N>> : public TLoaderForRef<std::array<T, N>> {
    public:
        using TLoaderForRef<std::array<T, N>>::TLoaderForRef;

        inline void EnsureArray() override {
        }

        TLoaderPtr AddArrayValue() override {
            if (Index_ == N) {
                ythrow TLoaderError() << "Too many elements. Only " << N << " are allowed";
            }
            return GetLoader(this->ValueRef_[Index_++]);
        }

        inline void Finish() override {
            if (Index_ < N) {
                ythrow TLoaderError() << "Need " << N << " element(s) but only " << Index_ << " are provided";
            }
        }
    private:
        size_t Index_ = 0;
    };

    template <class K, class V, class H, class E, class A>
    class TLoader<THashMap<K, V, H, E, A>> : public TLoaderForRef<THashMap<K, V, H, E, A>> {
    public:
        using TLoaderForRef<THashMap<K, V, H, E, A>>::TLoaderForRef;

        inline void EnsureMap() override {
        }

        TLoaderPtr AddMapValue(TStringBuf key) override {
            return GetLoader(this->ValueRef_[key]);
        }
    };

    template <class T>
    concept CHasGetMemberLoader = requires(T t) {
        { t.GetMemberLoader(TStringBuf{}) } -> std::same_as<TLoaderPtr>;
    };

    template <class T>
    concept CHasFinishMethod = requires(T t) {
        t.Finish();
    };

    template <class T>
    concept CHasGetMemberLoaderWithoutMemberInfo = CHasGetMemberLoader<T> && !CHasEdlMemberInfo<T>;

    template <CHasGetMemberLoaderWithoutMemberInfo T>
    class TLoader<T> : public TLoaderForRef<T> {
    public:
        using TLoaderForRef<T>::TLoaderForRef;

        inline void EnsureMap() override {
        }

        TLoaderPtr AddMapValue(TStringBuf key) override {
            auto loaderPtr = this->ValueRef_.GetMemberLoader(key);
            if (!loaderPtr) {
                ythrow TLoaderError() << "Unexpected map key '" << key << "'";
            }
            return loaderPtr;
        }

        void Finish() override {
            if constexpr (CHasFinishMethod<T>) {
                this->ValueRef_.Finish();
            }
        }
    };

    template <class T>
    class TLoader<THolder<T>> : public TLoader<T> {
    public:
        TLoader<THolder<T>>(THolder<T>& valPtr)
            : TLoader<T>((valPtr = MakeHolder<T>(), *valPtr))
        {
        }
    };

    template <class T>
    class TLoader<TIntrusivePtr<T>> : public TLoader<T> {
    public:
        TLoader<THolder<T>>(TIntrusivePtr<T>& valPtr)
            : TLoader<T>((valPtr = MakeIntrusive<T>(), *valPtr))
        {
        }
    };

    template <class T>
    class TLoader<TMaybe<T>> : public TLoader<T> {
    public:
        TLoader<TMaybe<T>>(TMaybe<T>& val)
            : TLoader<T>((val = T{}, *val))
        {
        }
    };

    template <>
    class TLoader<NJson::TJsonValue> : public TLoaderForRef<NJson::TJsonValue> {
    public:
        using TLoaderForRef<NJson::TJsonValue>::TLoaderForRef;

        inline void SetValue(std::nullptr_t) override {
            this->ValueRef_ = NJson::TJsonValue(NJson::JSON_NULL);
        }

        inline void SetValue(bool val) override {
            this->ValueRef_ = NJson::TJsonValue(val);
        }

        inline void SetValue(long long val) override {
            this->ValueRef_ = NJson::TJsonValue(val);
        }

        inline void SetValue(unsigned long long val) override {
            this->ValueRef_ = NJson::TJsonValue(val);
        }

        inline void SetValue(double val) override {
            this->ValueRef_ = NJson::TJsonValue(val);
        }

        inline void SetValue(const TStringBuf val) override {
            this->ValueRef_ = NJson::TJsonValue(val);
        }

        inline void EnsureMap() override {
            Y_ENSURE(!this->ValueRef_.IsDefined());
            this->ValueRef_ = NJson::TJsonValue(NJson::JSON_MAP);
        }

        inline TLoaderPtr AddMapValue(TStringBuf key) override {
            Y_ENSURE(this->ValueRef_.IsMap());
            return GetLoader(this->ValueRef_.InsertValue(key, NJson::TJsonValue{}));
        }

        inline void EnsureArray() override {
            Y_ENSURE(!this->ValueRef_.IsDefined());
            this->ValueRef_ = NJson::TJsonValue(NJson::JSON_ARRAY);
        }

        inline TLoaderPtr AddArrayValue() override {
            Y_ENSURE(this->ValueRef_.IsArray());
            return GetLoader(this->ValueRef_.AppendValue(NJson::TJsonValue{}));
        }
    };

    template <size_t I, class T>
    TLoaderPtr GetMemberLoader(T& self) {
        return GetLoader(T::template EdlMemberInfo<I>::GetRef(self));
    }

    template <class T, size_t... Is>
    THashMap<TString, TLoaderPtr(*)(T& self)> GetMemberLoaders(std::index_sequence<Is...>) {
        return {
            std::make_pair(T::template EdlMemberInfo<Is>::GetExportName(), &GetMemberLoader<Is, T>)...
        };
    }

    template <CHasEdlMemberInfo T>
    class TLoader<T> : public TLoaderForRef<T> {
    public:
        using TLoaderForRef<T>::TLoaderForRef;

        inline void EnsureMap() override {
        }

        TLoaderPtr AddMapValue(TStringBuf key) override {
            if (auto loaderGetter = Loaders_.FindPtr(key)) {
                return (*loaderGetter)(this->ValueRef_);
            }
            // Fallbacks to GetDefaultMemberRef() or GetMemberLoader() if possible
            if constexpr (CHasEdlDefaultMember<T>) {
                TLoaderPtr defaultMemberLoader = GetLoader(T::template GetDefaultMemberRef(this->ValueRef_));
                return defaultMemberLoader->AddMapValue(key);
            }
            if constexpr (CHasGetMemberLoader<T>) {
                if (TLoaderPtr loader = this->ValueRef_.GetMemberLoader(key)) {
                    return loader;
                }
            }
            ythrow TLoaderError() << "Unexpected map key '" << key << "'";
        }

        void Finish() override {
            if constexpr (CHasFinishMethod<T>) {
                this->ValueRef_.Finish();
            }
        }

    private:
        inline static THashMap<TString, TLoaderPtr(*)(T& self)> Loaders_ = GetMemberLoaders<T>(std::make_index_sequence<T::EdlMemberCount>());
    };
}
