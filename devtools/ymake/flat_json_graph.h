#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>

#include <library/cpp/json/writer/json.h>

namespace NFlatJsonGraph {

    template<typename>
    struct TNodePropertyTrait;

    template<typename TProp>
    concept NodeProperty = requires(NJsonWriter::TBuf& to, const TProp& prop) {
        { TNodePropertyTrait<std::remove_cvref_t<TProp>>::Serialize(to, prop) };
    };

    template<typename TRange>
    concept NodePropertiesRange = requires(const TRange& rng) {
            typename TRange::value_type;
            { rng.begin() };
            { rng.end() };
            { *rng.begin() } -> NodeProperty;
        };

    // Provided NodeProperty implementations
    template<>
    struct TNodePropertyTrait<TStringBuf> {
        static void Serialize(NJsonWriter::TBuf& to, TStringBuf value) {
            to.WriteString(value);
        }
    };
    template<>
    struct TNodePropertyTrait<TString>: TNodePropertyTrait<TStringBuf> {};
    template<size_t N>
    struct TNodePropertyTrait<char[N]>: TNodePropertyTrait<TStringBuf> {};

    template<>
    struct TNodePropertyTrait<ui32> {
        static void Serialize(NJsonWriter::TBuf& to, ui32 value) {
            to.WriteLongLong(value);
        }
    };

    template<NodePropertiesRange TRange>
    struct TNodePropertyTrait<TRange> {
        static void Serialize(NJsonWriter::TBuf& to, const TRange& value) {
            to.BeginList();
            for (const auto& item: value) {
                TNodePropertyTrait<std::remove_cvref_t<decltype(item)>>::Serialize(to, item);
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
            TNodePropertyTrait<TProp>::Serialize(JsonWriter, val);
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
        explicit TWriter(IOutputStream& sink, EIDFormat format = EIDFormat::Simple);
        ~TWriter();

        TNodeWriter AddNode(TConstDepNodeRef node);
        TNodeWriter AddLink(TConstDepRef dep);
        TNodeWriter AddLink(TConstDepNodeRef from, EDepType type, TConstDepNodeRef to);

    protected:
        TNodeWriter AddNode(const EMakeNodeType type, const ui32 id, const TStringBuf name);
        TNodeWriter AddLink(const ui32 fromId, const EMakeNodeType fromType, const ui32 toId, const EMakeNodeType toType, const EDepType depType, const ELogicalDepType logicalDepType = ELDT_FromDepType);

    private:
        void FinishNode(bool reopen);

    private:
        NJsonWriter::TBuf JsonWriter;
        EIDFormat Format;
        bool UnfinishedNode = false;
    };

}
