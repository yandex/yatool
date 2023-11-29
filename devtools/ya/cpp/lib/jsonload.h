#pragma once

#include <library/cpp/json/json_reader.h>

#include <util/folder/path.h>
#include <util/stream/file.h>
#include <util/system/file.h>

namespace NYa::NJsonLoad {
#undef CALL_JSON_METHOD
#undef WRAP2_NO_ARGS_METHOD
#undef WRAP1_NO_ARGS_METHOD
#undef WRAP_MACRO_SELECTOR
#undef WRAP_NO_ARGS_METHOD

// Wrap method call into lambda (to force method overload resolution) and pass the lambda to the private TWrappedJsonValue::Call method
// Note that decltype(auto) is important to save cvref of a returning value
#define CALL_JSON_METHOD(METHOD, ...)\
    Call([](const NJson::TJsonValue& jsonValue) -> decltype(auto) {return jsonValue.METHOD(__VA_ARGS__);})

#define WRAP_NO_ARGS_METHOD2(WRAPPER, ORIG) \
    auto WRAPPER() const { \
        return CALL_JSON_##METHOD(ORIG); \
    }
#define WRAP_NO_ARGS_METHOD1(WRAPPER) WRAP_NO_ARGS_METHOD2(WRAPPER, WRAPPER)

    class TWrappedJsonValue {
    private:
        // Calls TJsonValue method and provides more useful message on error.
        // Note: decltype(auto) is important to save cvref of a returning value.
        // Json value MUST be returned by ref here.
        template <class F>
        decltype(auto) Call(F f) const {
            try{
                return f(JsonValue_);
            } catch (NJson::TJsonException e) {
                throw yexception() << "Json load failed at '" << GetPath() << "' with error: " << e.what();
            }
        }

    public:
        explicit TWrappedJsonValue(const NJson::TJsonValue& jsonValue)
            : TWrappedJsonValue("", jsonValue, nullptr)
        {
        }

        // Only really used methods are added. Fill free to add missing ones if required
        WRAP_NO_ARGS_METHOD1(IsDefined)
        WRAP_NO_ARGS_METHOD2(GetBoolean, GetBooleanSafe)
        WRAP_NO_ARGS_METHOD2(GetInteger, GetIntegerSafe)
        WRAP_NO_ARGS_METHOD1(GetIntegerRobust)
        WRAP_NO_ARGS_METHOD2(GetString, GetStringSafe)
        WRAP_NO_ARGS_METHOD1(GetStringRobust)
        WRAP_NO_ARGS_METHOD1(GetType)

        auto Has(const TStringBuf key) const {
            return JsonValue_.Has(key);
        }

        template <class T>
        TWrappedJsonValue GetItem(T index) const {
            return TWrappedJsonValue(index, JsonValue_[index], this);
        }

        // Wrap map values in TWrappedJsonValue
        const THashMap<TString, TWrappedJsonValue> GetMap() const {
            THashMap<TString, TWrappedJsonValue> result{};
            const NJson::TJsonValue::TMapType& map = CALL_JSON_METHOD(GetMapSafe);
            for (const auto& [name, value] : map) {
                result.emplace(name, TWrappedJsonValue(name, value, this));
            }
            return result;
        }

        // Wrap array values in the TWrappedJsonValue
        const TVector<TWrappedJsonValue> GetArray() const {
            TVector<TWrappedJsonValue> result{};
            const NJson::TJsonValue::TArray& array = CALL_JSON_METHOD(GetArraySafe);
            for (size_t i = 0; i < array.size(); ++i) {
                result.push_back(TWrappedJsonValue(i, array[i], this));
            }
            return result;
        }

        const NJson::TJsonValue& GetJsonValue() const {
            return JsonValue_;
        }

        TString GetKey() const {
            if (auto item = std::get_if<TString>(&PathItem_)) {
                return *item;
            }
            throw yexception() << "Key is requested for not object item. Path: " << GetPath();
        }

        const TWrappedJsonValue* GetParent() const {
            return Parent_;
        }

        TString GetPath() const {
            TString pathItem = std::visit([](auto&& v) -> TString {return ToString(v);}, PathItem_);
            if (Parent_) {
                TString path = Parent_->GetPath();
                if (std::holds_alternative<size_t>(PathItem_)) {
                    path += "[" + pathItem + "]";
                } else {
                    if (path) {
                        path += ".";
                    }
                    path += pathItem;
                }
                return path;
            } else {
                return pathItem;
            }
        }

    private:
        using TJsonPathItem = std::variant<TString, size_t>;

        TWrappedJsonValue(TJsonPathItem pathItem, const NJson::TJsonValue& jsonValue, const TWrappedJsonValue* parent)
            : PathItem_{pathItem}
            , Parent_{parent}
            , JsonValue_{jsonValue}
        {
        }

        const TJsonPathItem PathItem_;
        const TWrappedJsonValue* Parent_;
        const NJson::TJsonValue& JsonValue_;
    };
#undef CALL_JSON_METHOD
#undef WRAP2_NO_ARGS_METHOD
#undef WRAP1_NO_ARGS_METHOD
#undef WRAP_MACRO_SELECTOR
#undef WRAP_NO_ARGS_METHOD

// Specialize FromJson() for concrete user types. It's a bit more convenient than TJsonDeserializer specialization.
// Specialize TJsonSerialize for generic types

    template <class T>
    struct TJsonDeserializer {
        static void Load(T& value, const TWrappedJsonValue& wrappedJson);
    };

    template <class T>
    inline void FromJson(T& value, const TWrappedJsonValue& wrappedJson) {
        TJsonDeserializer<T>::Load(value, wrappedJson);
    }

    template <class T>
    inline void FromJson(T& value, const TWrappedJsonValue& wrappedJson, const T& defaultValue) {
            if (wrappedJson.IsDefined()) {
                FromJson(value, wrappedJson);
            } else {
                value = defaultValue;
            }
    }

    template <class T>
    struct TJsonDeserializer<THashMap<TString, T>> {
        static void Load(THashMap<TString, T>& value, const TWrappedJsonValue& wrappedJson) {
            for (const auto& [name, jVal] : wrappedJson.GetMap()) {
                T item{};
                FromJson(item, jVal);
                value.emplace(name, std::move(item));
            }
        }
    };

    template <class T>
    struct TJsonDeserializer<TVector<T>> {
        static void Load(TVector<T>& value, const TWrappedJsonValue& wrappedJson) {
            for (const TWrappedJsonValue& jVal: wrappedJson.GetArray()) {
                T item{};
                FromJson(item, jVal);
                value.push_back(std::move(item));
            }
        }
    };

    template <class T>
    struct TJsonDeserializer<THashSet<T>> {
        static void Load(THashSet<T>& value, const TWrappedJsonValue& wrappedJson) {
            for (const TWrappedJsonValue& jVal: wrappedJson.GetArray()) {
                T item{};
                FromJson(item, jVal);
                value.insert(std::move(item));
            }
        }
    };

    template <class T>
    struct TJsonDeserializer<TMaybe<T>> {
        static void Load(TMaybe<T>& value, const TWrappedJsonValue& wrappedJson) {
            if (wrappedJson.IsDefined()) {
                T item{};
                FromJson(item, wrappedJson);
                value = std::move(item);
            } else {
                value.Clear();
            }
        }
    };

    template<>
    inline void FromJson(NJson::TJsonValue& value, const TWrappedJsonValue& wrappedJson) {
        value = wrappedJson.GetJsonValue();
    }

    template<>
    inline void FromJson(bool& value, const TWrappedJsonValue& wrappedJson) {
        value = wrappedJson.GetBoolean();
    }

    template<>
    inline void FromJson(long long& value, const TWrappedJsonValue& wrappedJson) {
        value = wrappedJson.GetInteger();
    }

    template<>
    inline void FromJson(TString& value, const TWrappedJsonValue& wrappedJson) {
        value = wrappedJson.GetString();
    }

    template<>
    inline void FromJson(TFsPath& value, const TWrappedJsonValue& wrappedJson) {
        value = TFsPath(wrappedJson.GetString());
    }

    template <class T>
    void LoadFromJsonValue(T& value, const NJson::TJsonValue& jsonValue) {
        FromJson(value, TWrappedJsonValue{jsonValue});
    }

    template <class T>
    void LoadFromBuffer(T& value, const TStringBuf data) {
        NJson::TJsonValue jsonValue;
        NJson::ReadJsonFastTree(data, &jsonValue, true);
        FromJson(value, TWrappedJsonValue{jsonValue});
    }

    template <class T>
    void LoadFromFile(T& value, const TFsPath& filePath) {
        TFileInput stream(TFile(filePath, OpenExisting | RdOnly));
        LoadFromBuffer(value, stream.ReadAll());
    }
}
