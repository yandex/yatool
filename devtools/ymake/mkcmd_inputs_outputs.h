#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/module_state.h>
#include <devtools/ymake/module_store.h>

#include <functional>

inline bool IsFakeModule(const TModules& modules, TDepTreeNode nodeVal) {
    if (IsModuleType(nodeVal.NodeType)) {
        return modules.Get(nodeVal.ElemId)->IsFakeModule();
    }
    return false;
}

inline TString RealPath(const TBuildConfiguration& conf, const TDepGraph& graph, const TConstDepNodeRef& node) {
    const auto& fileConf = graph.Names().FileConf;
    TFileView resolvedNode = fileConf.ResolveLink(graph.GetFileName(node));
    return conf.RealPath(resolvedNode);
}

template<
    bool onlyExplicitInputs,
    typename TAddInputFunc = std::function<void(const TConstDepRef&, bool)>,
    typename TAddSpanInputFunc = std::function<void(ui32, ui32)>,
    typename TIsGlobalSrcFunc = std::function<bool(const TConstDepNodeRef&)>,
    typename TAddOutputFunc = std::function<void(const TConstDepNodeRef&)>,
    typename TSetCmdFunc = std::function<void(const TConstDepNodeRef&)>
>
inline void ProcessInputsAndOutputs(
    const TConstDepNodeRef& node, bool isModule,
    const TModules& modules,
    TAddInputFunc&& addInput,
    TIsGlobalSrcFunc&& isGlobalSrc,
    TAddSpanInputFunc&& addSpanInput = TAddSpanInputFunc{},
    TAddOutputFunc&& addOutput = TAddOutputFunc{},
    TSetCmdFunc&& setCmd = TSetCmdFunc{}
) {
    size_t cnt = 0;
    size_t numDeps = node.Edges().Total();

    const TDepGraph& graph = TDepGraph::Graph(node);

    bool explicitInputs = true;
    TVector<ui32> inputCoordCounts;

    for (const auto dep : node.Edges()) {
        const auto depNode = dep.To();
        const auto depNodeVal = *dep.To();
        const auto depType = *dep;

        YDIAG(MkCmd) << cnt++ << " dep from " << numDeps << ": " << graph.ToString(depNode) << Endl;

        if (isModule && IsModuleType(depNodeVal.NodeType)) {
            continue;
        }

        if (depType == EDT_Group) {
            TStringBuf depName = graph.GetCmdName(depNode).GetStr();
            explicitInputs = depName == NStaticConf::INPUTS_MARKER;
            continue;
        }

        if constexpr (onlyExplicitInputs) {
            if (!explicitInputs) {
                continue;
            }
        }

        if (depType == EDT_BuildFrom && UseFileId(depNodeVal.NodeType) && !IsFakeModule(modules, depNodeVal)) {
            if (isModule) {
                if (!IsDirType(depNodeVal.NodeType)) {
                    bool depIsGlobalSrc = isGlobalSrc(depNode);

                    if (explicitInputs) {
                        Y_ASSERT(!depIsGlobalSrc);
                    }

                    if (!depIsGlobalSrc) {
                        addInput(depNode, explicitInputs);
                        if (explicitInputs) {
                            inputCoordCounts.push_back(1);
                        }
                    }
                }

            } else {
                addInput(depNode, explicitInputs);
                if (explicitInputs) {
                    inputCoordCounts.push_back(1);
                }
            }

            continue;
        }

        if (IsIndirectSrcDep(dep)) {
            Y_ASSERT(explicitInputs);

            ui32 lateGlobSize = 0;
            for (const auto& depdep: dep.To().Edges()) {
                if (*depdep == EDT_Property && depdep.To()->NodeType == EMNT_File) {
                    addInput(depdep.To(), explicitInputs);
                    lateGlobSize++;
                }
            }
            inputCoordCounts.push_back(lateGlobSize);
            continue;
        }

        if constexpr (!onlyExplicitInputs) {
            if (depType == EDT_OutTogetherBack) {
                if (!IsFakeModule(modules, depNodeVal)) {
                    addOutput(depNode);
                }
                continue;
            }

            if (depNodeVal.NodeType == EMNT_BuildCommand && depType == EDT_BuildCommand) {
                setCmd(depNode);
                continue;
            }
        }
    }

    if constexpr (!onlyExplicitInputs) {
        ui32 inputId = 0;
        for (ui32 inputSize : inputCoordCounts) {
            addSpanInput(inputId, inputSize);
            inputId += inputSize;
        }
    }
}

template<typename TGetModuleDirFunc>
inline TString InputToPath(const TBuildConfiguration& conf, const TConstDepNodeRef& inputNode, TGetModuleDirFunc&& getModuleDir) {
    const TDepGraph& graph = TDepGraph::Graph(inputNode);

    TString inputPath = RealPath(conf, graph, inputNode);
    if (!inputPath.Empty()) {
        return inputPath;
    }

    // If input file is $U - the best effort is to resolve it in CurDir.
    const TFileConf& fileConf = graph.Names().FileConf;
    const TFileView depName = fileConf.ResolveLink(graph.GetFileName(inputNode));

    auto moduleDir = getModuleDir();

    YErr() << depName << ": resolve this input file in current source dir " << moduleDir << ". Be ready for build problems." << Endl;
    return conf.RealPath(NPath::Join(moduleDir, depName.CutType()));
}
