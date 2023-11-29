#pragma once

#include "ymake.h"

Y_FORCE_INLINE void TNodeAddCtx::AddDep(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName) {
    AddDep(depType, elemNodeType, Graph.Names().AddName(elemNodeType, elemName));
}

Y_FORCE_INLINE void TNodeAddCtx::AddDep(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) {
    Y_ASSERT(elemId);
    Deps.Add(depType, elemNodeType, elemId);
}

Y_FORCE_INLINE void TNodeAddCtx::AddDepIface(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName) {
    AddDep(depType, elemNodeType, elemName);
}

Y_FORCE_INLINE void TNodeAddCtx::AddDepIface(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) {
    AddDep(depType, elemNodeType, elemId);
}

Y_FORCE_INLINE bool TNodeAddCtx::AddUniqueDep(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName) {
    return AddUniqueDep(depType, elemNodeType, Graph.Names().AddName(elemNodeType, elemName));
}

Y_FORCE_INLINE bool TNodeAddCtx::AddUniqueDep(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) {
    Y_ASSERT(elemId);
    return Deps.AddUnique(depType, elemNodeType, elemId);
}

Y_FORCE_INLINE void TNodeAddCtx::AddDepsUnique(const TPropsNodeList& what, EDepType depType, EMakeNodeType nodeType) {
    for (auto propNode : what) {
        Deps.AddUnique(depType, nodeType, ::ElemId(propNode));
    }
}

Y_FORCE_INLINE TAddDepAdaptor& TNodeAddCtx::AddOutput(ui64 elemId, EMakeNodeType defaultType, bool addToOwn) {
    auto i = UpdIter.Nodes.Insert(MakeDepsCacheId(defaultType, elemId), &YMake, Module);
    TNodeAddCtx& add = *i->second.AddCtx;
    if (add.NodeType == EMNT_Deleted) {
        add.NodeType = defaultType;
        add.ElemId = elemId;
    }

    i->second.SetReassemble(true); // note: this is for output files only, commands reassemble based on CmdOrigin
    if (addToOwn) {
        Y_ASSERT(UseFileId(defaultType));
        TModAddData& modData = add.GetModuleData();
        modData.AlreadyAdded = !Module->AddEntry(elemId);
        modData.Added = true;
        i->second.OnceEntered = false;
        if (i->second.AddCtx && !i->second.AddCtx->NeedInit2) {
            // This was already processed, thus unusable
            Y_ASSERT(!i->second.AddCtxOwned);
            i->second.AddCtx = nullptr;
        }
    }
    return add;
}

Y_FORCE_INLINE TPropertiesState& TNodeAddCtx::GetProps() {
    return GetEntry().Props;
}
