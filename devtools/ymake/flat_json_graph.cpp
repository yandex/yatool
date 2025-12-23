#include "flat_json_graph.h"

#include <fmt/format.h>

namespace NFlatJsonGraph {

    TWriter::TWriter(IOutputStream& sink)
        : JsonWriter{NJsonWriter::HEM_RELAXED, &sink}
    {
        JsonWriter.SetIndentSpaces(2);
        JsonWriter.BeginObject();
        JsonWriter.WriteKey("data");
        JsonWriter.BeginList();
    }

    TWriter::~TWriter() {
        FinishNode(false);
        JsonWriter.EndList();
        JsonWriter.EndObject();
    }

    TNodeWriter TWriter::AddNode(TConstDepNodeRef node) {
        return AddNode(node->NodeType, node->ElemId, TDepGraph::Graph(node).ToTargetStringBuf(node), EIDFormat::Simple);
    }

    TNodeWriter TWriter::AddNode(const EMakeNodeType type, const ui32 id, const TStringBuf name, const EIDFormat format) {
        FinishNode(true);
        JsonWriter.BeginObject();
        JsonWriter.WriteKey("DataType");
        JsonWriter.WriteString("Node");
        JsonWriter.WriteKey("Id");
        if (format == EIDFormat::Complex) {
            JsonWriter.WriteString(CreateComplexId(type, id));
        } else {
            JsonWriter.WriteInt(id);
        }
        JsonWriter.WriteKey("Name");
        JsonWriter.WriteString(name);
        JsonWriter.WriteKey("NodeType");
        JsonWriter.WriteString(TStringBuilder() << type);

        return TNodeWriter{JsonWriter};
    }

    TNodeWriter TWriter::AddLink(TConstDepRef dep) {
        return AddLink(dep.From()->ElemId, dep.From()->NodeType, dep.To()->ElemId, dep.To()->NodeType, dep.Value(), EIDFormat::Simple);
    }

    TNodeWriter TWriter::AddLink(const ui32 fromId, const EMakeNodeType fromType, const ui32 toId, const EMakeNodeType toType, const EDepType depType, const EIDFormat format, const ELogicalDepType logicalDepType) {
        FinishNode(true);
        JsonWriter.BeginObject();
        JsonWriter.WriteKey("DataType");
        JsonWriter.WriteString("Dep");
        JsonWriter.WriteKey("FromId");
        if (format == EIDFormat::Complex) {
            JsonWriter.WriteString(CreateComplexId(fromType, fromId));
        } else {
            JsonWriter.WriteInt(fromId);
        }
        JsonWriter.WriteKey("ToId");
        if (format == EIDFormat::Complex) {
            JsonWriter.WriteString(CreateComplexId(toType, toId));
        } else {
            JsonWriter.WriteInt(toId);
        }
        JsonWriter.WriteKey("DepType");
        if (logicalDepType == ELDT_FromDepType) {
            JsonWriter.WriteString(TStringBuilder() << depType);
        } else {
            JsonWriter.WriteString(TStringBuilder() << logicalDepType);
        }
        return TNodeWriter{JsonWriter};
    }

    void TWriter::FinishNode(bool reopen) {
        if (std::exchange(UnfinishedNode, reopen)) {
            JsonWriter.EndObject();
        }
    }

    TString TWriter::CreateComplexId(EMakeNodeType type, ui32 id) const {
        ui32 targetId = TFileConf::GetTargetId(id);
        TStringBuf nodeContext = "n";
        if (UseFileId(type) && targetId != id) {
            nodeContext = TFileConf::GetContextStr(id);
        }
        return fmt::format("{}:{}:{}", UseFileId(type) ? "f" : "c", nodeContext, targetId);
    }

    // "Unit tests" for provided NodeProperty implementations
    static_assert(NodeProperty<TStringBuf>);
    static_assert(!NodePropertiesRange<TStringBuf>);
    static_assert(NodeProperty<decltype("hello")>);
    static_assert(!NodePropertiesRange<decltype("hello")>);
    static_assert(NodeProperty<TString>);
    static_assert(!NodePropertiesRange<TString>);
    static_assert(NodePropertiesRange<TVector<TString>>);
    static_assert(NodeProperty<TVector<TString>>);

}
