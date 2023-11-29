#include "node_builder.h"

#include "macro_string.h"
#include "macro_processor.h"

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/dep_graph.h>

#include <devtools/ymake/common/npath.h>

TDepRef TNodeBuilder::AddDep(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName) {
    return AddDep(depType, elemNodeType, Names.AddName(elemNodeType, elemName));
}

TDepRef TNodeBuilder::AddDep(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) {
    auto& graph = TDepGraph::Graph(Node);
    TDepNodeRef newNode = graph.GetNodeById(elemNodeType, elemId);
    if (newNode.IsValid()) {
        AssertEx(IsTypeCompatibleWith(elemNodeType, newNode->NodeType), "Attempt to create homonym node");
        return Node.AddEdge(newNode.Id(), depType);
    } else {
        return Node.AddEdge(graph.AddNode(elemNodeType, elemId).Id(), depType);
    }
}

void TNodeBuilder::AddDepIface(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName) {
    AddDep(depType, elemNodeType, elemName);
}

void TNodeBuilder::AddDepIface(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) {
    AddDep(depType, elemNodeType, elemId);
}

bool TNodeBuilder::AddUniqueDep(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName) {
    AddUniqueDep(depType, elemNodeType, Names.AddName(elemNodeType, elemName));
    return true;
}

bool TNodeBuilder::AddUniqueDep(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) {
    auto& graph = TDepGraph::Graph(Node);
    TDepNodeRef newNode = graph.GetNodeById(elemNodeType, elemId);
    if (newNode.IsValid()) {
        AssertEx(IsTypeCompatibleWith(elemNodeType, newNode->NodeType), "Attempt to create homonym node");
        Node.AddUniqueEdge(newNode.Id(), depType);
        return true;
    } else {
        //Dep to new node will always be unique
        Node.AddEdge(graph.AddNode(elemNodeType, elemId).Id(), depType);
        return true;
    }

}

void TNodeBuilder::AddParsedIncls(TStringBuf type, const TVector<TResolveFile>& files) {
    // This is partly copied from TNodeAddCtx::AddParsedIncls
    if (files.empty()) {
        return;
    }

    // ParsedIncls can be added from cmd-node (if in conf there is "include" option) and from file-node.
    // this property will be added by name that is formed with id of parent node =>
    // if there is a clash by id in FileConf and CommandConf we will have a clash by name and
    // extra deps somewhere (in file or in cmd)
    // so we can modify property value (as unused in general) to avoid clash by name
    TDepRef propDep = AddDep(EDT_Property, EMNT_BuildCommand,
                             FormatCmd(Node->ElemId, TString::Join("ParsedIncls.", type),
                                       UseFileId(Node->NodeType) ? "" : "cmdOrigin"));

    TNodeBuilder propNode{Names, propDep.To()};
    for (const auto& file : files) {
        propNode.AddUniqueDep(EDT_Include, FileTypeByRoot(file.Root()), file.GetTargetId());
    }
}

void TNodeBuilder::AddDirsToProps(const TDirs& dirs, TStringBuf propName) {
    // This is partly copied from TNodeAddCtx::AddDirsToProps
    // FIXME(spreis): Passing directories via properties doesn't look right overall and better to be
    // replaced by some other way to pass directories
    if (dirs.empty()) {
        return;
    }

    TDepRef propDep = AddDep(EDT_Property, EMNT_BuildCommand, FormatCmd(ElemId, propName, ""));
    TNodeBuilder propNode{Names, propDep.To()};
    for (const auto& d : dirs) {
        propNode.AddUniqueDep(EDT_Include, EMNT_Directory, d.GetElemId());
    }
}

void TNodeBuilder::AddDirsToProps(const TVector<ui32>&, TStringBuf) {
    ythrow yexception() << "AddDirsToProps: Not implemented yet for a new graph";
}

void TNodeBuilder::AddDirsToProps(const TPropsNodeList&, TStringBuf) {
    ythrow yexception() << "AddDirsToProps: Not implemented yet for a new graph";
}
