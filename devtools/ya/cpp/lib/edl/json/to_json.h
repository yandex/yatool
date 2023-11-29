#pragma once

#include <devtools/ya/cpp/lib/edl/common/export_helpers.h>

#include <library/cpp/json/json_writer.h>

#include <util/stream/output.h>


namespace NYa::NEdl {
    // Forward declarations
    class TJsonExporter;

    template <class T>
    void ToJson(NJson::TJsonWriter& writer, const T&);

    template <class T>
    void ToParentJson(TJsonExporter& parent, const T&);

    class TJsonExporter {
    public:
        TJsonExporter(NJson::TJsonWriter& writer)
            : Writer_{writer}
        {
        }

        void ExportNullValue() {
            Writer_.WriteNull();
        }

        template <class T>
        void ExportValue(const T& val) {
            ToJson(Writer_, val);
        }

        template <class T>
        void ExportInnerValue(const T& val) {
            ToParentJson(*this, val);
        }

        template <class Iter>
        void ExportRange(Iter b, Iter e) {
            Writer_.OpenArray();
            while (b != e) {
                ToJson(Writer_, *b);
                ++b;
            }
            Writer_.CloseArray();
        }

        void OpenMap() {
            Writer_.OpenMap();
        }

        template <class V>
        void AddMapItem(TStringBuf key, const V& val) {
            Writer_.WriteKey(key);
            ToJson(Writer_, val);
        }

        void CloseMap() {
            Writer_.CloseMap();
        }

    private:
        NJson::TJsonWriter& Writer_;
    };

    class TInnerJsonExporter {
    public:
        TInnerJsonExporter(TJsonExporter& parent)
            : Parent_{parent}
        {
        }

        void ExportNullValue() {
            // Do nothing if inner object is null
        }

        template <class T>
        void ExportValue(const T& val) {
            ToParentJson(Parent_, val);
        }

        void OpenMap() {
        }

        template <class V>
        void AddMapItem(TStringBuf key, const V& val) {
            Parent_.AddMapItem(key, val);
        }

        void CloseMap() {
        }

    private:
        TJsonExporter& Parent_;
    };


    template <class T>
    struct TJsonifier {
        static void ToJson(NJson::TJsonWriter& writer, const T& val) {
            TJsonExporter exp{writer};
            Export(exp, val);
        }

        static void ToParentJson(TJsonExporter& parent, const T& val) {
            TInnerJsonExporter exp{parent};
            Export(exp, val);
        }
    };

    template <class T>
    void ToParentJson(TJsonExporter& parent, const T& val) {
        TJsonifier<T>::ToParentJson(parent, val);
    }

    template <class T>
    concept CSupportedByWriter = requires(T val) {
        std::declval<NJson::TJsonWriter>().Write(val);
    };

    template <CSupportedByWriter T>
    struct TJsonifier<T> {
        static void ToJson(NJson::TJsonWriter& writer, const T& val) {
            writer.Write(val);
        }
    };

    template <class T>
    void ToJson(NJson::TJsonWriter& writer, const T& val) {
        TJsonifier<T>::ToJson(writer, val);
    }

    template <class T>
    void ToJson(IOutputStream& stream, const T& val, bool formatOutput=false) {
        NJson::TJsonWriter writer{&stream, formatOutput};
        ToJson(writer, val);
        writer.Flush();
    }

    template <class T>
    requires (!std::is_base_of_v<IOutputStream, T>) // Prevent ambiguity with previous template if T is bool
    TString ToJson(const T& val, bool formatOutput=false) {
        TStringStream stream;
        NJson::TJsonWriter writer{&stream, formatOutput};
        ToJson(writer, val);
        writer.Flush();
        return stream.Str();
    }
}
