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
#include <tuple>
#include <variant>
#include <utility>

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
            return GetLoader(this->ValueRef_[K{key}]);
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

    #define Y_SET_VALUE_METHOD(...) \
        void SetValue(__VA_ARGS__ val) override { \
            DoSetValue(val); \
        }

    namespace {
        template <size_t N>
        class TVariantNestedLoader;

        template <size_t N>
        struct TVariantLoaderHelper {
            TVariantLoaderHelper(std::array<TLoaderPtr, N>&& loaders, std::array<TLoaderPtr, N>& variantLoaders)
                : Loaders{std::move(loaders)}
                , VariantLoadersRef{variantLoaders}
            {
            }

            TVariantLoaderHelper(std::array<TLoaderPtr, N>&& loaders)
                : Loaders{std::move(loaders)}
                , VariantLoadersRef{Loaders}
            {
            }

            void EnsureMap() {
                Apply([&](size_t idx) {
                    Loaders[idx]->EnsureMap();
                    return true;
                });
            }

            void EnsureArray() {
                Apply([&](size_t idx) {
                    Loaders[idx]->EnsureArray();
                    return true;
                });
            }

            TLoaderPtr AddMapValue(TStringBuf key) {
                return MakeHolder<TVariantNestedLoader<N>>(MakeNestedLoaders(&TBaseLoader::AddMapValue, key), VariantLoadersRef);
            }

            TLoaderPtr AddArrayValue() {
                return MakeHolder<TVariantNestedLoader<N>>(MakeNestedLoaders(&TBaseLoader::AddArrayValue), VariantLoadersRef);
            }

            using ApplyFunc = std::function<bool(size_t)>;
            void Apply(const ApplyFunc& func) {
                bool success = false;
                TString lastError{};
                for (size_t i = 0; i < N; ++i) {
                    if (auto& variantLoader = VariantLoadersRef[i]) {
                        try {
                            if (!func(i)) {
                                return;
                            }
                            success = true;
                        } catch (const yexception& e) {
                            lastError = e.what();
                            variantLoader.Reset();
                        }
                    }
                }
                if (success) {
                    return;
                }
                ythrow TLoaderError() << "No suitable variant alternative found. Last error: " << lastError;
            }

            template <class... Args>
            std::array<TLoaderPtr, N> MakeNestedLoaders(TLoaderPtr (TBaseLoader::*func)(Args...), Args... args) {
                std::array<TLoaderPtr, N> loaders{};
                Apply([&](size_t idx) {
                    loaders[idx] = ((*Loaders[idx]).*func)(args...);
                    return true;
                });
                return loaders;
            }

            std::array<TLoaderPtr, N> Loaders;
            std::array<TLoaderPtr, N>& VariantLoadersRef;
        };

        template <size_t N>
        class TVariantNestedLoader : public TBaseLoader {
        public:
            TVariantNestedLoader(std::array<TLoaderPtr, N>&& loaders, std::array<TLoaderPtr, N>& variantLoaders)
                : Helper_{std::move(loaders), variantLoaders}
            {
            }

            Y_SET_VALUE_METHOD(std::nullptr_t)
            Y_SET_VALUE_METHOD(bool)
            Y_SET_VALUE_METHOD(long long)
            Y_SET_VALUE_METHOD(unsigned long long)
            Y_SET_VALUE_METHOD(double)
            Y_SET_VALUE_METHOD(const TStringBuf)

            void EnsureMap() override {
                Helper_.EnsureMap();
            }

            TLoaderPtr AddMapValue(TStringBuf key) override {
                return Helper_.AddMapValue(key);
            }

            void EnsureArray() override {
                Helper_.EnsureArray();
            }

            TLoaderPtr AddArrayValue() override {
                return Helper_.AddArrayValue();
            }

            void Finish() override {
                Helper_.Apply([&](size_t idx) {
                    Helper_.Loaders[idx]->Finish();
                    return true;
                });
            }

        private:
            template <class T>
            void DoSetValue(T val) {
                Helper_.Apply([&](size_t idx) {
                    Helper_.Loaders[idx]->SetValue(val);
                    return true;
                });
            }

        private:
            TVariantLoaderHelper<N> Helper_;
        };
    }

    // Universal variant loader.
    // The loader is relatively SLOW because uses exceptions and `try catch` to find a proper alternative.
    // Don't use it in a hot path.
    template <class... V>
    class TLoader<std::variant<V...>> : public TLoaderForRef<std::variant<V...>> {
    private:
        static constexpr size_t N = sizeof...(V);

    public:
        TLoader(std::variant<V...>& val)
            : TLoaderForRef<std::variant<V...>>(val)
            , Helper_{
                std::apply([&](auto&... items) {
                    return std::array<TLoaderPtr, N>{GetLoader(items)...};
                }, Items_)
            }
        {
        }

        Y_SET_VALUE_METHOD(std::nullptr_t)
        Y_SET_VALUE_METHOD(bool)
        Y_SET_VALUE_METHOD(long long)
        Y_SET_VALUE_METHOD(unsigned long long)
        Y_SET_VALUE_METHOD(double)
        Y_SET_VALUE_METHOD(const TStringBuf)

        void EnsureMap() override {
            Helper_.EnsureMap();
        }

        TLoaderPtr AddMapValue(TStringBuf key) override {
            return Helper_.AddMapValue(key);
        }

        void EnsureArray() override {
            Helper_.EnsureArray();
        }

        TLoaderPtr AddArrayValue() override {
            return Helper_.AddArrayValue();
        }

        void Finish() override {
            Helper_.Apply([&](size_t idx) {
                Helper_.Loaders[idx]->Finish();
                SetAlternative(idx);
                return false;
            });
        }

    private:
        template <class T>
        void DoSetValue(T val) {
            Helper_.Apply([&](size_t idx) {
                Helper_.Loaders[idx]->SetValue(val);
                SetAlternative(idx);
                return false;
            });
        }

        template <size_t DestIdx>
        inline bool SetAlternativeHelper(size_t idx) {
            if (DestIdx != idx) {
                return true;
            }
            this->ValueRef_ = std::move(std::get<DestIdx>(Items_));
            return false;
        }

        void SetAlternative(size_t idx) {
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                (SetAlternativeHelper<Is>(idx) && ...);
            } (std::make_index_sequence<N>{});
        }

    private:
        std::tuple<V...> Items_{};
        TVariantLoaderHelper<N> Helper_;
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
        TLoader(T& val) : TLoaderForRef<T>(val)
        {
            if constexpr (CHasEdlDefaultMember<T>) {
                DefaultMemberLoader_ = GetLoader(T::template GetDefaultMemberRef<T>(this->ValueRef_));
            }
        }

        inline void EnsureMap() override {
            if constexpr (CHasEdlDefaultMember<T>) {
                DefaultMemberLoader_->EnsureMap();
            }
        }

        TLoaderPtr AddMapValue(TStringBuf key) override {
            if (auto loaderGetter = Loaders_.FindPtr(key)) {
                return (*loaderGetter)(this->ValueRef_);
            }
            // Fallbacks to GetDefaultMemberRef() or GetMemberLoader() if possible
            if constexpr (CHasEdlDefaultMember<T>) {
                return DefaultMemberLoader_->AddMapValue(key);
            }
            if constexpr (CHasGetMemberLoader<T>) {
                if (TLoaderPtr loader = this->ValueRef_.GetMemberLoader(key)) {
                    return loader;
                }
            }
            ythrow TLoaderError() << "Unexpected map key '" << key << "'";
        }

        void Finish() override {
            if constexpr (CHasEdlDefaultMember<T>) {
                DefaultMemberLoader_->Finish();
            }
            if constexpr (CHasFinishMethod<T>) {
                this->ValueRef_.Finish();
            }
        }

    private:
        inline static THashMap<TString, TLoaderPtr(*)(T& self)> Loaders_ = GetMemberLoaders<T>(std::make_index_sequence<T::EdlMemberCount>());
        TLoaderPtr DefaultMemberLoader_{};
    };
}
