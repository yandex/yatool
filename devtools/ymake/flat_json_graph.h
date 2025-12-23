#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>

#include <library/cpp/json/writer/json.h>

namespace NFlatJsonGraph {

    template<typename>
    struct TNodePropertyTriat;

    template<typename TProp>
    concept NodeProperty = requires(NJsonWriter::TBuf& to, const TProp& prop) {
        { TNodePropertyTriat<std::remove_cvref_t<TProp>>::Serialize(to, prop) };
    };

    template<typename TRange>
    concept NodePropertiesRange = requires(const TRange& rng) {
            typename TRange::value_type;
            { rng.begin() };
            { rng.end() };
            { *rng.begin() } -> NodeProperty;
        };

    // Provided NodePropety implementations
    template<>
    struct TNodePropertyTriat<TStringBuf> {
        static void Serialize(NJsonWriter::TBuf& to, TStringBuf value) {
            to.WriteString(value);
        }
    };
    template<>
    struct TNodePropertyTriat<TString>: TNodePropertyTriat<TStringBuf> {};
    template<size_t N>
    struct TNodePropertyTriat<char[N]>: TNodePropertyTriat<TStringBuf> {};

    template<>
    struct TNodePropertyTriat<ui32> {
        static void Serialize(NJsonWriter::TBuf& to, ui32 value) {
            to.WriteLongLong(value);
        }
    };

    template<NodePropertiesRange TRange>
    struct TNodePropertyTriat<TRange> {
        static void Serialize(NJsonWriter::TBuf& to, const TRange& value) {
            to.BeginList();
            for (const auto& item: value) {
                TNodePropertyTriat<std::remove_cvref_t<decltype(item)>>::Serialize(to, item);
            }
            to.EndList();
        }
    };

    class TNodeWriter {
    public:
        TNodeWriter(NJsonWriter::TBuf& jsonWriter): JsonWriter{jsonWriter} {}

        template<NodeProperty TProp>
        TNodeWriter& AddProp(TStringBuf name, const TProp& val) {
            JsonWriter.WriteKey(name);
            TNodePropertyTriat<TProp>::Serialize(JsonWriter, val);
            return *this;
        }

        TNodeWriter& AddProp(TStringBuf name, bool val) {
            JsonWriter.WriteKey(name);
            JsonWriter.WriteBool(val);
            return *this;
        }

        TNodeWriter& AddProp(TStringBuf name, const std::string& val) {
            JsonWriter.WriteKey(name);
            JsonWriter.WriteString(val);
            return *this;
        }

        TNodeWriter& AddProp(TStringBuf name, i64 val) {
            JsonWriter.WriteKey(name);
            JsonWriter.WriteLongLong(val);
            return *this;
        }

        TNodeWriter& AddProp(TStringBuf name, ui64 val) {
            JsonWriter.WriteKey(name);
            JsonWriter.WriteULongLong(val);
            return *this;
        }

    private:
        NJsonWriter::TBuf& JsonWriter;
    };

    enum class EIDFormat {
        Simple,
        Complex
    };

    class TWriter {
    public:
        explicit TWriter(IOutputStream& sink);
        ~TWriter();

        TNodeWriter AddNode(TConstDepNodeRef node);
        TNodeWriter AddNode(const EMakeNodeType type, const ui32 id, const TStringBuf name, const EIDFormat format);

        TNodeWriter AddLink(TConstDepRef dep);
        TNodeWriter AddLink(const ui32 fromId, const EMakeNodeType fromType, const ui32 toId, const EMakeNodeType toType, const EDepType depType, const EIDFormat format, const ELogicalDepType logicalDepType = ELDT_FromDepType);

    private:
        void FinishNode(bool reopen);
        TString CreateComplexId(EMakeNodeType type, ui32 id) const;

    private:
        NJsonWriter::TBuf JsonWriter;
        bool UnfinishedNode = false;
    };

}
