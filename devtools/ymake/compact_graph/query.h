#pragma once

#include "dep_graph.h"
#include "iter.h"
#include <devtools/ymake/prop_names.h>

/// @brief Find module node in depth-first iter state
/// According to latest version of code we only ensure that this is not a sole node
/// Legacy assert ensuring that Module has Directory parent would look like this:
/// Y_ASSERT(*(ret+1).Node.NodeType == EMNT_Directory);
/// Now module can be argument of macro such as SYMLINK etc->no directory before it in stack
template <typename TIterState>
typename TIterState::TConstIterator FindModule(const TIterState& state) {
    if (state.Size() < 2) {
        return state.end();
    }
    return state.FindRecent([](const typename TIterState::TItem& item) {
        return IsModuleType(item.Node()->NodeType);
    });
}

template <class T>
bool IsModule(const T& st) {
    return IsModuleType(st.Node()->NodeType);
}

template <class T>
bool IsMakeFile (const T& st) {
    return IsMakeFileType(st.Node()->NodeType);
}

inline bool IsRecurseDep(EMakeNodeType fromType, EDepType depType, EMakeNodeType toType) {
    return fromType == EMNT_Directory && (depType == EDT_Include || depType == EDT_BuildFrom || depType == EDT_Search) && IsDirType(toType);
}

template <class TDep>
bool IsRecurseDep(const TDep& dep) {
    return IsRecurseDep(dep.From()->NodeType, *dep, dep.To()->NodeType);
}

inline bool IsDependsDep(EMakeNodeType fromType, EDepType depType, EMakeNodeType toType) {
    return fromType == EMNT_Directory && depType == EDT_BuildFrom && IsDirType(toType);
}

template <class TDep>
bool IsDependsDep(const TDep& dep) {
    return IsDependsDep(dep.From()->NodeType, *dep, dep.To()->NodeType);
}

inline bool IsPureRecurseDep(EMakeNodeType fromType, EDepType depType, EMakeNodeType toType) {
    return fromType == EMNT_Directory && (depType == EDT_Include || depType == EDT_Search) && IsDirType(toType);
}

template <class TDep>
bool IsPureRecurseDep(const TDep& dep) {
    return IsPureRecurseDep(dep.From()->NodeType, *dep, dep.To()->NodeType);
}

inline bool IsTestRecurseDep(EMakeNodeType fromType, EDepType depType, EMakeNodeType toType) {
    return fromType == EMNT_Directory && depType == EDT_Search && IsDirType(toType);
}

template <class TDep>
bool IsTestRecurseDep(const TDep& dep) {
    return IsRecurseDep(dep.From()->NodeType, *dep, dep.To()->NodeType);
}

inline TConstDepNodeRef GetOutTogetherDependency(const TConstDepNodeRef& node) {
    for (const auto& edge : node.Edges()) {
        if (*edge == EDT_OutTogether) {
            return edge.To();
        }
    }
    return TDepGraph::GetInvalidNode(node);
}

inline bool IsDirectPeerdirDep(EMakeNodeType nodeFrom, EDepType dep, EMakeNodeType nodeTo) {
    return IsModuleType(nodeFrom) && IsModuleType(nodeTo) && (dep == EDT_Include || dep == EDT_BuildFrom);
}

template <class TDep>
bool IsDirectPeerdirDep(const TDep& dep) {
    return IsDirectPeerdirDep(dep.From()->NodeType, *dep, dep.To()->NodeType);
}

inline bool IsPeerdirDep(EMakeNodeType nodeFrom, EDepType dep, EMakeNodeType nodeTo) {
    return IsModuleType(nodeFrom) && IsDirType(nodeTo) && (dep == EDT_Include || dep == EDT_BuildFrom);
}

template <class TDep>
bool IsPeerdirDep(const TDep& dep) {
    return IsPeerdirDep(dep.From()->NodeType, *dep, dep.To()->NodeType);
}

// Must be no EDT_Property in stack
inline bool IsTooldirDep(EMakeNodeType nodeFrom, EDepType dep, EMakeNodeType nodeTo) {
    return nodeTo == EMNT_Directory && dep == EDT_Include && nodeFrom == EMNT_BuildCommand;
}

template <class TDep>
bool IsTooldirDep(const TDep& dep) {
    return IsTooldirDep(dep.From()->NodeType, *dep, dep.To()->NodeType);
}

inline bool IsDirectToolDep(EMakeNodeType nodeFrom, EDepType dep, EMakeNodeType nodeTo) {
    return nodeFrom == EMNT_BuildCommand && dep == EDT_Include && IsModuleType(nodeTo);
}

template <class TDep>
bool IsDirectToolDep(const TDep& dep) {
    return IsDirectToolDep(dep.From()->NodeType, *dep, dep.To()->NodeType);
}

template <class TDep>
bool IsGlobalSrcDep(const TDep& dep) {
    return IsModuleType(dep.From()->NodeType) && *dep == EDT_Search2 && IsFileType(dep.To()->NodeType);
}

inline bool IsSearchDirDep(EMakeNodeType /* nodeFrom */, EDepType dep, EMakeNodeType nodeTo) {
    bool isSearchDep = dep == EDT_Search || dep == EDT_Search2;
    return isSearchDep && IsDirType(nodeTo);
}

template <class TDep>
bool IsSearchDirDep(const TDep& dep) {
    return IsSearchDirDep(dep.From()->NodeType, *dep, dep.To()->NodeType);
}

template <class TDep>
bool IsPropToDirSearchDep(const TDep& dep) {
    return dep.From()->NodeType == EMNT_BuildCommand && IsSearchDirDep(dep);
}

// generated files from whose a module is built
template <class TDep>
Y_FORCE_INLINE bool IsModuleOwnNodeDep(const TDep& dep) {
    EDepType depType = *dep;
    bool mayOwn = depType == EDT_BuildFrom ||
                  depType == EDT_OutTogether ||
                  depType == EDT_OutTogetherBack ||
                  (IsModuleType(dep.From()->NodeType) && (depType == EDT_Search || depType == EDT_Search2));
    return mayOwn && dep.To()->NodeType == EMNT_NonParsedFile;
}

// generated files from whose a module is built and their source files
template <class TDep>
Y_FORCE_INLINE bool IsModuleSrcDep(const TDep& dep) {
    EMakeNodeType fromType = dep.From()->NodeType;
    if (fromType == EMNT_File) {
        return false;
    }
    EDepType depType = *dep;
    bool srcDep = depType == EDT_BuildFrom ||
                  depType == EDT_OutTogether ||
                  depType == EDT_OutTogetherBack ||
                  (IsModuleType(fromType) && depType == EDT_Search);

    EMakeNodeType toType = dep.To()->NodeType;
    return srcDep && (toType == EMNT_NonParsedFile || toType == EMNT_File);
}

// Indirect source dependency: EMNT_*File -[EDT_BuildFrom]-> EMNT_BuildCommand -[EDT_Property]-> EMNT_File
// used in GLOBs.
template <class TDep>
Y_FORCE_INLINE bool IsIndirectSrcDep(const TDep& dep) {
    return
        (IsFileType(dep.From()->NodeType) && *dep == EDT_BuildFrom && dep.To()->NodeType == EMNT_BuildCommand);
}

template <class TDep>
Y_FORCE_INLINE bool IsDartPropDep(const TDep& dep) {
    return
        (!IsFileType(dep.From()->NodeType) && *dep == EDT_BuildFrom && dep.To()->NodeType == EMNT_BuildCommand);
}

template <class TDep>
Y_FORCE_INLINE bool IsAllSrcsPropDep(const TDep& dep) {
    return
        IsModuleType(dep.From()->NodeType)
        && *dep == EDT_Property
        && dep.To()->NodeType == EMNT_BuildCommand
        && TDepGraph::GetCmdName(dep.To()).GetStr().Contains(NProps::ALL_SRCS);
}

template <class TDep>
Y_FORCE_INLINE bool IsLateGlobPropDep(const TDep& dep) {
    return
        IsModuleType(dep.From()->NodeType)
        && *dep == EDT_Property
        && dep.To()->NodeType == EMNT_BuildCommand
        && TDepGraph::GetCmdName(dep.To()).GetStr().Contains(NProps::LATE_GLOB);
}

template <class TDep>
bool IsDirToModuleDep(const TDep& dep) {
    return dep.From()->NodeType == EMNT_Directory && *dep == EDT_Include && IsModuleType(dep.To()->NodeType);
}

template <class TDep>
bool IsInnerCommandDep(const TDep& dep) {
    return dep.From()->NodeType == EMNT_BuildCommand && *dep == EDT_Include && dep.To()->NodeType == EMNT_BuildCommand;
}

template <class TDep>
bool IsLocalVariableDep(const TDep& dep) {
    return IsFileType(dep.From()->NodeType) && *dep == EDT_BuildCommand && dep.To()->NodeType == EMNT_BuildVariable;
}

template <class TDep>
bool IsBuildCommandDep(const TDep& dep) {
    return IsFileType(dep.From()->NodeType) && *dep == EDT_BuildCommand && dep.To()->NodeType == EMNT_BuildCommand;
}

template <class TDep>
bool IsLoopGenDep(const TDep& dep) {
    if (*dep == EDT_Search || *dep == EDT_OutTogetherBack || *dep == EDT_Property) {
        return false;
    }
    if (*dep == EDT_Search2 && !IsGlobalSrcDep(dep)) {
        return false;
    }
    if (IsRecurseDep(dep)) {
        return false;
    }
    return true;
}

inline bool IsMakeFilePropertyDep(EMakeNodeType nodeFrom, EDepType dep, EMakeNodeType nodeTo) {
    return nodeFrom == EMNT_MakeFile && dep == EDT_Property && nodeTo == EMNT_BuildCommand;
}

inline bool IsPropertyDep(EMakeNodeType, EDepType dep, EMakeNodeType nodeTo) {
    return dep == EDT_Property && (nodeTo == EMNT_Property || nodeTo == EMNT_BuildCommand);
}

template <class TDep>
bool IsPropertyDep(const TDep& dep) {
    return IsPropertyDep(dep.From()->NodeType, *dep, dep.To()->NodeType);
}

inline bool IsModulePropertyDep(EMakeNodeType nodeFrom, EDepType dep, EMakeNodeType nodeTo) {
    return IsModuleType(nodeFrom) && IsPropertyDep(nodeFrom, dep, nodeTo);
}

inline bool IsIncludeFileDep(EMakeNodeType nodeFrom, EDepType dep, EMakeNodeType nodeTo) {
    return IsSrcFileType(nodeFrom) && dep == EDT_Include && IsSrcFileType(nodeTo);
}

template <class TDep>
bool IsIncludeFileDep(const TDep& dep) {
    return IsIncludeFileDep(dep.From()->NodeType, *dep, dep.To()->NodeType);
}

inline bool IsMakeFileIncludeDep(EMakeNodeType nodeFrom, EDepType dep, EMakeNodeType nodeTo) {
    return nodeFrom == EMNT_MakeFile && dep == EDT_Include && IsSrcFileType(nodeTo);
}

inline bool IsBuildCmdInclusion(EMakeNodeType nodeFrom, EDepType dep, EMakeNodeType nodeTo) {
    return (IsModuleType(nodeFrom) || nodeFrom == EMNT_NonParsedFile) && dep == EDT_Include && nodeTo == EMNT_BuildCommand;
}

inline bool IsPropertyFileDep(EDepType dep, EMakeNodeType nodeTo) {
    return dep == EDT_Property && IsSrcFileType(nodeTo);
}

template <class TDep>
bool IsPropertyFileDep(const TDep& dep) {
    return IsPropertyFileDep(*dep, dep.To()->NodeType);
}

template <class TDep>
bool IsBuildCmdInclusion(const TDep& dep) {
    return IsBuildCmdInclusion(dep.From()->NodeType, *dep, dep.To()->NodeType);
}

inline TConstDepNodeRef GetDepNodeWithType(const TConstDepNodeRef& node, EDepType depType, EMakeNodeType nodeType) {
    for (const auto& edge : node.Edges()) {
        if (*edge == depType && edge.To()->NodeType == nodeType) {
            return edge.To();
        }
    }
    return TDepGraph::GetInvalidNode(node);
}

inline TNodeId GetDepNodeWithType(TNodeId nodeId, const TDepGraph& graph, EDepType depType, EMakeNodeType nodeType) {
    return GetDepNodeWithType(graph.Get(nodeId), depType, nodeType).Id();
}

// $LS aka DirLister properties are unique per MakeFile so we can look back deeply
template<class V>
bool DirListPropDep(const V& stack) {
    if (stack.size() < 3)
        return false;
    const auto& pst = stack[stack.size() - 2];
    const auto& mst = stack[stack.size() - 3];
    return IsDirType(stack.back().Node.NodeType) && pst.Node.NodeType == EMNT_BuildCommand && mst.Node.NodeType == EMNT_MakeFile && mst.Dep.DepType == EDT_Property;
}
