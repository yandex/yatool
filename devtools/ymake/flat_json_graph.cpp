#include "flat_json_graph.h"

#include <fmt/format.h>

namespace {

TString CreateComplexId(EMakeNodeType type, TFileElemId id) {
    TFileElemId targetId = TFileConf::GetTargetId(id);
    TStringBuf nodeContext = "n";
    if (UseFileId(type) && targetId != id) {
        nodeContext = TFileConf::GetContextStr(id);
    }
    return fmt::format("{}:{}:{}", UseFileId(type) ? "f" : "c", nodeContext, RawElemId(targetId));
}

}

namespace NFlatJsonGraph {

    TWriter::TWriter(IOutputStream& sink, EIDFormat format)
        : JsonWriter{NJsonWriter::HEM_RELAXED, &sink}
        , Format{format}
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
        return AddNode(node->NodeType, node->ElemId, TDepGraph::Graph(node).ToTargetStringBuf(node));
    }

    TNodeWriter TWriter::AddNode(const EMakeNodeType type, const TElemId id, const TStringBuf name) {
        FinishNode(true);
        JsonWriter.BeginObject();
        JsonWriter.WriteKey("DataType");
        JsonWriter.WriteString("Node");
        JsonWriter.WriteKey("Id");
        switch (Format) {
            case EIDFormat::Simple: JsonWriter.WriteInt(RawElemId(id)); break;
            case EIDFormat::Complex: JsonWriter.WriteString(CreateComplexId(type, AssumeFile(id))); break;
        }
        JsonWriter.WriteKey("Name");
        JsonWriter.WriteString(name);
        JsonWriter.WriteKey("NodeType");
        JsonWriter.WriteString(TStringBuilder() << type);

        return TNodeWriter{JsonWriter};
    }

    TNodeWriter TWriter::AddLink(TConstDepRef dep) {
        return AddLink(dep.From()->ElemId, dep.From()->NodeType, dep.To()->ElemId, dep.To()->NodeType, dep.Value());
    }

    TNodeWriter TWriter::AddLink(TConstDepNodeRef from, EDepType type, TConstDepNodeRef to) {
        return AddLink(from->ElemId, from->NodeType, to->ElemId, to->NodeType, type);
    }

    TNodeWriter TWriter::AddLink(const TElemId fromId, const EMakeNodeType fromType, const TElemId toId, const EMakeNodeType toType, const EDepType depType, const ELogicalDepType logicalDepType) {
        FinishNode(true);
        JsonWriter.BeginObject();
        JsonWriter.WriteKey("DataType");
        JsonWriter.WriteString("Dep");
        JsonWriter.WriteKey("FromId");
        switch (Format) {
            case EIDFormat::Simple: JsonWriter.WriteInt(RawElemId(fromId)); break;
            case EIDFormat::Complex: JsonWriter.WriteString(CreateComplexId(fromType, AssumeFile(fromId))); break;
        }
        JsonWriter.WriteKey("ToId");
        switch (Format) {
            case EIDFormat::Simple: JsonWriter.WriteInt(RawElemId(toId)); break;
            case EIDFormat::Complex: JsonWriter.WriteString(CreateComplexId(toType, AssumeFile(toId))); break;
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
