#pragma once

#include <devtools/ymake/diag/stats.h>

#include <util/generic/string.h>
#include <util/generic/strbuf.h>
#include <util/generic/ymath.h>
#include <util/generic/yexception.h>
#include <util/stream/output.h>
#include <util/stream/format.h>
#include <util/string/cast.h>

#include <concepts>

namespace NCache {
    class TConversionContext;
}

namespace NYMake {
    class TJsonWriter;

    /// Value is some string/string_view type
    template<typename K>
    concept CStringView = std::convertible_to<K, const TStringBuf&>;

    /// Value is vector/list/deque
    template<typename V>
    concept CArrValue = std::ranges::input_range<V> && requires(V v, V::value_type value) {
        v.emplace_back(value);
    };

    // Value is map/unordered_map/set
    template<typename V>
    concept CMapValue = std::ranges::input_range<V> && requires(V v, V::key_type key, V::mapped_type value) {
        v.contains(key);
        v.emplace(std::make_pair(key, value));
    };

    // Value is struct/class with WriteAsJson function
    template<typename V>
    concept CWriteAsJson = requires(V v, TJsonWriter& jsonWriter, const NCache::TConversionContext* context) {
        v.WriteAsJson(jsonWriter, context);
    };

    class TJsonWriter {
    public:
        /// Context of opened array/map
        template<typename TOpenedTag>
        struct TOpened {
            int ValueIndex{0};
        };
        struct TArrayTag;
        using TOpenedArray = TOpened<TArrayTag>;///< Context of opened array
        struct TMapTag;
        using TOpenedMap = TOpened<TMapTag>;///< Context of opened map

        TJsonWriter(IOutputStream& out);
        ~TJsonWriter();

        /// Write key of map, always wait some string/string_view value
        template<CStringView K>
        inline void WriteMapKey(TOpenedMap& map, const K& key) {
            WriteValueSeparatorIfNeeded(map.ValueIndex++);
            WriteString(key);
            WriteKeySeparator();
        }

        /// Write some integer value
        template<std::integral T>
        inline void WriteValue(const T value) {
            WriteDirectly(ToString<T>(value));
        }

        /// Write some number with floating point value
        void WriteValue(const float value);
        void WriteValue(const double value);

        /// Write some string/string_view value
        template<CStringView V>
        inline void WriteValue(const V& value) {
            WriteString(value);
        }

        /// Write array (vector/list/deque) value
        template<CArrValue V>
        inline void WriteValue(const V& values) {
            auto arr = OpenArray();
            for (const auto& value: values) {
                WriteArrayValue(arr, value);
            }
            CloseArray(arr);
        }

        /// Write map (map/unordered_map/set) value
        template<CMapValue V>
        inline void WriteValue(const V& values) {
            auto map = OpenMap();
            for (const auto& [key, value]: values) {
                WriteMapKeyValue(map, key, value);
            }
            CloseMap(map);
        }

        /// Write struct/class with WriteAsJson function value
        template<CWriteAsJson V>
        inline void WriteValue(const V& value) {
            value.WriteAsJson(*this, nullptr);
        }

        /// Write struct/class with WriteAsJson function value with conversion context
        template<CWriteAsJson V>
        inline void WriteValue(const V& value, const NCache::TConversionContext* context) {
            value.WriteAsJson(*this, context);
        }

        /// Write array item value
        template<typename V>
        inline void WriteArrayValue(TOpenedArray& openedArray, const V& value) {
            WriteValueSeparatorIfNeeded(openedArray.ValueIndex++);
            WriteValue(value);
        }

        /// Write array item value with conversion context
        template<CWriteAsJson V>
        inline void WriteArrayValue(TOpenedArray& openedArray, const V& value, const NCache::TConversionContext* context) {
            WriteValueSeparatorIfNeeded(openedArray.ValueIndex++);
            WriteValue(value, context);
        }

        /// Write array item JSON value (direct write string/string_view without escaping)
        template<CStringView V>
        inline void WriteArrayJsonValue(TOpenedArray& openedArray, const V& value) {
            WriteValueSeparatorIfNeeded(openedArray.ValueIndex++);
            WriteJsonValue(value);
        }

        /// Write map key with value
        template<CStringView K, typename V>
        inline void WriteMapKeyValue(TOpenedMap& map, const K& key, const V& value) {
            WriteMapKey<K>(map, key);
            WriteValue(value);
        }

        /// Write map key with JSON value (direct write string/string_view without escaping)
        template<CStringView K, CStringView V>
        inline void WriteMapKeyJsonValue(TOpenedMap& map, const K& key, const V& value) {
            WriteMapKey<K>(map, key);
            WriteJsonValue(value);
        }

        /// Write JSON value (direct write string/string_view without escaping)
        template<CStringView V>
        inline void WriteJsonValue(const V& value) {
            WriteDirectly(value);
        }

        /// Open array and return context
        /// Attention!!! This class does NOT monitor the correct nesting of arrays/objects, the user must monitor this himself
        inline TOpenedArray OpenArray() {
            WriteDirectly('[');
            return {};
        }

        /// Open array as item of other array and return context
        /// Attention!!! This class does NOT monitor the correct nesting of arrays/objects, the user must monitor this himself
        inline TOpenedArray OpenArray(TOpenedArray& arr) {
            WriteValueSeparatorIfNeeded(arr.ValueIndex++);
            WriteDirectly('[');
            return {};
        }

        /// Close array
        /// Attention!!! This class does NOT monitor the correct nesting of arrays/objects, the user must monitor this himself
        inline void CloseArray(TOpenedArray&) {
            WriteDirectly(']');
        }

        /// Open map and return context
        /// Attention!!! This class does NOT monitor the correct nesting of arrays/objects, the user must monitor this himself
        inline TOpenedMap OpenMap() {
            WriteDirectly('{');
            return {};
        }

        /// Open map as item of array and return context
        /// Attention!!! This class does NOT monitor the correct nesting of arrays/objects, the user must monitor this himself
        inline TOpenedMap OpenMap(TOpenedArray& arr) {
            WriteValueSeparatorIfNeeded(arr.ValueIndex++);
            WriteDirectly('{');
            return {};
        }

        /// Close map
        /// Attention!!! This class does NOT monitor the correct nesting of arrays/objects, the user must monitor this himself
        inline void CloseMap(TOpenedMap&) {
            WriteDirectly('}');
        }

        /// Flush all to output stream
        inline void Flush() {
            FlushBuf();
            Out_.Flush();
        }

    protected:
        IOutputStream& Out_;///< Stream for output JSON
        char Buf_[2048ull];///< Buffer for escaping and short strings
        char* Cur_;///< Current pointer in buffer

        void WriteString(const TStringBuf& s);///< Write string with quotes and escaping

        void WriteDirectly(const char c);///< Write char to buffer without escaping
        void WriteDirectly(const TStringBuf& s);///< Write string to buffer or to output stream without escaping

        inline void WriteValueSeparatorIfNeeded(bool writeValueSeparator = true) {
            if (writeValueSeparator) {
                WriteDirectly(',');
            }
        }

        inline void WriteKeySeparator() {
            WriteDirectly(':');
        }

        inline void WriteDirectly(const char* s) {
            WriteDirectly(TStringBuf(s));
        }

        template<CStringView T>
        inline void WriteDirectly(const T& s) {
            WriteDirectly(TStringBuf(s));
        }

        /// Flush internal buffer to output stream if not empty
        inline void FlushBuf() {
            if (Cur_ > Buf_) {
                DoFlushBuf();
            }
        }

        /// Really flush internal buffer
        inline void DoFlushBuf() {
            Out_ << TStringBuf(Buf_, Cur_ - Buf_);
            Cur_ = Buf_;
        }
    };
}
