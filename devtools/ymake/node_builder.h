#pragma once

#include "module_add_data.h"
#include "induced_props.h"
#include "module_state.h"

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/dep_graph.h>

#include <devtools/ymake/common/uniq_vector.h>

#include <util/generic/yexception.h>

class TNodeBuilder : public TAddDepAdaptor {
private:
    TDepNodeRef Node;
    TSymbols& Names;

public:
    TNodeBuilder(TSymbols& names, TDepNodeRef node)
        : Node(node)
        , Names(names)
    {
        NodeType = node->NodeType;
        ElemId = node->ElemId;
        UpdNode = node.Id(); // FIXME???
    }

    TDepRef AddDep(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName);
    TDepRef AddDep(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId);

    void AddDepIface(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName) final;
    void AddDepIface(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) final;

    bool AddUniqueDep(EDepType, EMakeNodeType, ui64) final;
    bool AddUniqueDep(EDepType, EMakeNodeType, TStringBuf) final;

    bool HasAnyDeps() const final {
        return !Node.Edges().IsEmpty();
    }

    void AddDeps(const TDeps& deps) final {
        for (const auto& dep: deps) {
            AddDep(dep.DepType, dep.NodeType, dep.ElemId);
        }
    }

    void AddParsedIncls(TStringBuf type, const TVector<TResolveFile>& files) final;

    void AddDirsToProps(const TDirs& dirs, TStringBuf propName) final;
    void AddDirsToProps(const TVector<ui32>& dirIds, TStringBuf propName) final;
    void AddDirsToProps(const TPropsNodeList& props, TStringBuf propName) final;

    TNodeBuilder& AddOutput(ui64, EMakeNodeType, bool = true) final {
        ythrow yexception() << "AddOutput: Not implemented yet for a new graph";

        /* TODO(spreis) implement
        // Old implementation is:
        auto i = YMake.UpdIter.Nodes.Insert(fileId, this);
        TNodeAddCtx& add = *i->second.AddCtx;
        i->second.Reassemble = true;
        if (addToOwn) {
            OwnEntries.insert(fid);
            i->second.SetOnceEntered(false);
        }
        return add;
        */
    }

    bool IsAvailable(TStringBuf) const {
        ythrow yexception() << "IsAvailable: Not implemented yet for a new graph";
        /* TODO(spreis) implement
        // Old implementation is:
        ui64 fid = YMake.Names.FileConf.GetIdNx(s);
        ui64 id = 0;
        if (fid != 0) {
            if (YMake.UseNewGraph) { // This will be set from start if new graph is built (Rename?)
                id = YMake.Graph.GetFileNodeById(id);
            } else {
                id = YMake.Graph.FileNodeById(id);
            }
        }
        return id && !ModuleData.OldFiles.has(fid) || fid && ModuleData.OwnEntries.has(fid);
        */
    }

    TPropertiesState& GetProps() final {
        ythrow yexception() << "GetProps: Not implemented yet for a new graph";
        /* TODO(spreis) implement or eliminate
        // Old implementation is:
        return GetOrInit(Entry.InducedProps);
        */
    }

    TModAddData& GetModuleData() final {
        ythrow yexception() << "GetModuleData: Not implemented yet for a new graph";
        /* TODO(spreis) implement or eliminate
        // Old implementation is:
        const auto cacheId = MakeDepsCacheId(NodeType, ElemId);
        TModAddData* modData = UpdIter.GetAddedModuleInfo(cacheId);
        Y_ASSERT(modData != nullptr);
        return *modData;
        */
    }

    TDepNodeRef& GetNode() { return Node; }
    const TDepNodeRef& GetNode() const { return Node; }
    const TIndDepsRule* SetDepsRuleByName(TStringBuf) final {
        ythrow yexception() << "SetDepsRuleByName: Not implemented yet for a new graph";
    }

    TModAddData ModuleData;
};
