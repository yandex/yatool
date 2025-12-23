#include "dump_graph_info.h"

#include "conf.h"
#include "node_printer.h"
#include "ymake.h"
#include "dependency_management.h"
#include "spdx.h"
#include "transitive_requirements_check.h"

static void PrintAllFiles(const TYMake& ymake) {
    YInfo() << "All Files:" << Endl;
    const TDepGraph& graph = ymake.Graph;

    // This format of dump used in devtools/ya/test/tests/lib/clone, on change, please, fix it too
    for (auto node : graph.Nodes()) {
        if (IsFileType(node->NodeType)) {
            TString nameWithCtx;
            graph.GetFileName(node).GetStr(nameWithCtx);
            YInfo() << "file: " << node->NodeType << ' ' << nameWithCtx << Endl;
        }
    }
}

static void PrintAllDirs(const TYMake& ymake) {
    YInfo() << "All Directories:" << Endl;
    const TDepGraph& graph = ymake.Graph;

    // This format of dump used in devtools/ya/test/tests/lib/clone, on change, please, fix it too
    for (auto node : graph.Nodes()) {
        if (UseFileId(node->NodeType) && !IsFileType(node->NodeType)) {
            TStringBuf name = graph.GetFileName(node).GetTargetStr();
            YInfo() << "dir: " << node->NodeType << ' ' << name << Endl;
        }
    }
}

static void PrintFlatGraph(const TYMake& ymake) {
    YInfo() << "Flat graph dump:" << Endl;
    const TDepGraph& graph = ymake.Graph;

    for (auto node : graph.Nodes()) {
        TString name = (node->NodeType != EMNT_Deleted || node->ElemId != 0) ? graph.ToString(node) : "<Null>";
        TString flags = DumpNodeFlags(node->ElemId, node->NodeType, ymake.Names);
        Cout << node.Id() << ' ' << node->NodeType << ' ' << name << '(' << node->ElemId << ")";
        if (!flags.empty())
            Cout << " - [" << flags << "]";
        Cout << " - " << node.Edges().Total() << " deps:";
        Cout << Endl;

        for (auto dep : node.Edges()) {
            Cout << "  "
                 << "[" << dep.Index() << "] "
                 << "-- " << *dep << " --> " << dep.To().Id() << ' ' << dep.To()->NodeType << ' ' << graph.ToString(dep.To()) << '(' << dep.To()->ElemId << ')' << Endl;
        }
    }
    if (ymake.Conf.DumpPretty)
        Cout << Endl;
}

EBuildResult DumpLicenseInfo(TBuildConfiguration& conf) {
    NSPDX::EPeerType peerType;
    if (conf.LicenseLinkType == "static") {
        peerType = NSPDX::EPeerType::Static;
    } else if (conf.LicenseLinkType == "dynamic") {
        peerType = NSPDX::EPeerType::Dynamic;
    } else {
        YErr() << "Invalid link type for license properties dump '" << conf.LicenseLinkType << "'. Only 'static' or 'dynamic' allowed." << Endl;
        return BR_FATAL_ERROR;
    }
    DoDumpLicenseInfo(conf, conf.CommandConf, peerType, conf.DumpLicensesInfo, conf.LicenseTagVars);
    return BR_OK;
}

EBuildResult PrintAbsTargetPath(const TBuildConfiguration& conf) {
    Y_ASSERT(conf.Targets.size() > 0);
    TFsPath firstBuildTarget = conf.BuildRoot / conf.Targets.front();
    Cout << firstBuildTarget << Endl;
    return BR_OK;
}

void PerformDumps(const TBuildConfiguration& conf, TYMake& yMake) {
    if (!conf.ManagedDepTreeRoots.empty()) {
        THashSet<TNodeId> roots;
        yMake.ResolveRelationTargets(conf.ManagedDepTreeRoots, roots);
        ExplainDM(yMake.GetRestoreContext(), roots);
    }

    if (!conf.DumpDMRoots.empty()) {
        THashSet<TNodeId> roots;
        yMake.ResolveRelationTargets(conf.DumpDMRoots, roots);
        DumpDM(
            yMake.GetRestoreContext(),
            roots,
            conf.DumpDirectDM ? EManagedPeersDepth::Direct : EManagedPeersDepth::Transitive
        );
    }

    if (conf.DumpRecurses || conf.DumpPeers) {
        yMake.DumpDependentDirs(conf.Cmsg());
    }

    if (conf.DumpDependentDirs) {
        yMake.DumpDependentDirs(conf.Cmsg(), conf.SkipDepends);
    }
    if (conf.DumpTargetDepFiles) {
        yMake.PrintTargetDeps(conf.Cmsg());
    }
    if (conf.PrintTargets) {
        yMake.DumpBuildTargets(conf.Cmsg());
    }
    if (conf.DumpSrcDeps) {
        yMake.DumpSrcDeps(conf.Cmsg());
    }

    if (conf.FullDumpGraph) {
        if (conf.DumpFiles) {
            PrintAllFiles(yMake);
        } else if (conf.DumpDirs) {
            PrintAllDirs(yMake);
        } else if (conf.DumpAsDot) {
            YConfErr(UserErr) << "-xGD is not implemented, use -xgD.." << Endl;
        } else {
            PrintFlatGraph(yMake);
        }
    }

    if (conf.DumpGraphStuff) {
        yMake.DumpGraph();
    }

    if (conf.DumpExpressions) {
        YInfo() << "Expression dump:" << Endl;
        yMake.Commands.ForEachCommand([&](ECmdId id, const NPolexpr::TExpression& expr) {
            yMake.Conf.Cmsg()
                << static_cast<std::underlying_type_t<ECmdId>>(id) << " "
                << yMake.Commands.PrintCmd(expr) << "\n";
        });
        if (yMake.Conf.DumpPretty)
            yMake.Conf.Cmsg() << Endl;
    }

    if (conf.DumpModulesInfo) {
        if (conf.ModulesInfoFile.empty()) {
            DumpModulesInfo(conf.Cmsg(), yMake.GetRestoreContext(), yMake.StartTargets, conf.ModulesInfoFilter);
        } else {
            TFileOutput out{conf.ModulesInfoFile};
            DumpModulesInfo(out, yMake.GetRestoreContext(), yMake.StartTargets, conf.ModulesInfoFilter);
        }
    }

    if (conf.DumpNames) {
        yMake.Names.Dump(conf.Cmsg());
    }

    if (!conf.FindPathTo.empty()) {
        yMake.FindPathBetween(conf.FindPathFrom, conf.FindPathTo);
    }

    if (conf.WriteOwners) {
        yMake.DumpOwners();
    }
}
