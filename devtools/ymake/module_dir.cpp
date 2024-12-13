#include "module_dir.h"

#include "add_dep_adaptor_inline.h"
#include "conf.h"
#include "prop_names.h"
#include "ymake.h"
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/include_processors/ts_processor.h>

void TModuleDirBuilder::UseModuleProps(TPropertyType propType, const TPropsNodeList& propsValues) {
    if (propType == TPropertyType{Graph.Names(), EVI_ModuleProps, "PeerDirs"} && !propsValues.empty()) {
        auto& parsedPeerdirs = GetOrInit(Node.GetModuleData().ParsedPeerdirs);
        for (auto d : propsValues) {
            Y_ASSERT(IsFile(d)); // `d' must not contain command cache id, GetName will fail or return garbage
            AddPeerdir(Graph.GetFileNameByCacheId(d).GetTargetStr());
            parsedPeerdirs.insert(ElemId(d));
        }
        return;
    }

    if (propType == TPropertyType{Graph.Names(), EVI_ModuleProps, "TsDeduceOut"} && !propsValues.empty()) {
        const TTsImportProcessor::TTsConfig cfg(Module);

        for (const auto& i : propsValues) {
            auto const sourcePath = GetPropertyValue(Graph.GetCmdNameByCacheId(i).GetStr());
            if (!NPath::IsPrefixOf(cfg.RootDir, sourcePath)) {
                continue;
            }

            auto const outputPaths = TTsImportProcessor::GenerateOutputPaths(sourcePath, cfg);
            for (auto& outputPath : outputPaths) {
                auto outputPathElemId = Graph.Names().AddName(EMNT_NonParsedFile, outputPath);
                TAddDepAdaptor& addCtx = Node.AddOutput(outputPathElemId, EMNT_NonParsedFile);
                addCtx.AddDepIface(EDT_OutTogether, EMNT_NonParsedFile, Node.ElemId);
                Node.AddUniqueDep(EDT_OutTogetherBack, EMNT_NonParsedFile, outputPathElemId);
            }
        }
    }
}

void TModuleDirBuilder::AddDepends(const TVector<TStringBuf>& args) {
    if (args.empty()) {
        // Do nothing
        return;
    }
    auto& props = Node.GetProps();
    TVector<TDepsCacheId> dirProps{Reserve(args.size())};
    for (const auto& arg : args) {
        const TString dirName = NPath::ConstructYDir(arg, TStringBuf(), ConstrYDirDiag);
        if (dirName.empty() || !Graph.Names().FileConf.CheckDirectory(dirName)) {
            TScopedContext context(Module.GetName());
            TRACE(P, NEvent::TInvalidRecurse(dirName));
            YConfErr(BadDir) << "DEPENDS to non-directory " << dirName << Endl;
            continue;
        }
        auto dirView = Graph.Names().FileConf.GetStoredName(dirName);
        dirProps.push_back(MakeDepFileCacheId(dirView.GetElemId()));
        auto dirNameWoType = NPath::CutType(dirName);
        Module.Vars.SetAppend("TEST_DEPENDS_VALUE", dirNameWoType);
        FORCE_UNIQ_CONFIGURE_TRACE(dirView, H, NEvent::TNeedDirHint(TString{dirNameWoType}));
    }
    TPropertySourceDebugOnly sourceDebug{EPropertyAdditionType::Created};
    TPropertyType dependsPropertyType{Graph.Names(), EVI_GetModules, NProps::DEPENDS};
    props.AddValues(dependsPropertyType, dirProps, sourceDebug);
    Node.AddDirsToProps(props.GetValues(dependsPropertyType), NProps::DEPENDS);
}

void TModuleDirBuilder::AddMissingDir(TStringBuf dir) {
    TFileView dirEnt = Graph.Names().FileConf.GetStoredName(dir);
    if (!Module.MissingDirs) {
        Module.MissingDirs = MakeHolder<TDirs>();
    }
    Module.MissingDirs->Push(dirEnt);
}

void TModuleDirBuilder::AddSrcdir(const TStringBuf& dir) {
    YDIAG(DG) << "Srcdir dep for module: " << dir << Endl;

    if (Graph.Names().FileConf.IsNonExistedSrcDir(dir)) {
        TScopedContext context(Module.GetName());
        TRACE(P, NEvent::TInvalidSrcDir(TString{dir}));
        YConfErr(BadDir) << "[[alt1]]SRCDIR[[rst]] to non existent directory " << dir << Endl;
        AddMissingDir(dir);
        return;
    }
    TFileView dirEnt = Graph.Names().FileConf.GetStoredName(dir);
    Module.SrcDirs.Push(dirEnt);
    FORCE_UNIQ_CONFIGURE_TRACE(dirEnt, H, NEvent::TNeedDirHint(TString{NPath::CutType(dir)}));
}

void TModuleDirBuilder::AddDataPath(TStringBuf path) {
    TFileView pathEnt = Graph.Names().FileConf.GetStoredName(ArcPath(path));
    if (!Module.DataPaths) {
        Module.DataPaths = MakeHolder<TDirs>();
    }
    Module.DataPaths->Push(pathEnt);
    YDIAG(DG) << "DATA dep for module: " << path << Endl;
}

void TModuleDirBuilder::AddIncdir(const TStringBuf& dir, EIncDirScope scope, bool checkDir, TLangId langId) {
    YDIAG(DG) << "Incdir dep for module: " << dir << Endl;

    if (checkDir && Graph.Names().FileConf.IsNonExistedSrcDir(dir)) {
        TScopedContext context(Module.GetName());
        TRACE(P, NEvent::TInvalidAddIncl(TString{dir}));
        if (ReportMissingAddincls) {
            YConfErr(BadDir) << "[[alt1]]ADDINCL[[rst]] to non existent source directory " << dir << Endl;
        } else {
            YConfWarn(BadDir) << "[[alt1]]ADDINCL[[rst]] to non existent source directory " << dir << Endl;
        }
        AddMissingDir(dir);
        return;
    }

    TFileView dirEnt = Graph.Names().FileConf.GetStoredName(dir);
    Module.IncDirs.Add(dirEnt, scope, langId);
}

void TModuleDirBuilder::AddPeerdir(const TStringBuf& dir, TFlags<EPeerOption> addFlags) {
    YDIAG(DG) << "Peerdir dep for module: " << dir << Endl;
    TFileView dirEnt = Graph.Names().FileConf.GetStoredName(dir);
    if (Module.Peers.has(dirEnt) && (addFlags & (EPeerOption::MaterialGhost | EPeerOption::VirtualGhost))) {
        return;
    }

    if (!Graph.Names().FileConf.CheckDirectory(dir)) { // non-existent and non-usable dirs are checked in another place
        TScopedContext context(Module.GetName());
        TRACE(P, NEvent::TInvalidPeerdir(TString{dir}));
        YConfErr(BadDir) << "[[alt1]]PEERDIR[[rst]] to non-directory " << dir << Endl;
        return;
    }

    if (addFlags & EPeerOption::MaterialGhost) {
        Module.GhostPeers.emplace(dirEnt.GetElemId(), EGhostType::Material);
    } else if (addFlags & EPeerOption::VirtualGhost) {
        Module.GhostPeers.emplace(dirEnt.GetElemId(), EGhostType::Virtual);
        return;
    } else {
        Module.GhostPeers.erase(dirEnt.GetElemId());
    }

    if (!Module.Peers.Push(dirEnt)) {
        return;
    }

    if (addFlags & EPeerOption::AddIncl) {
        AddIncdir(dirEnt.GetTargetStr(), EIncDirScope::Local, false);
    }
    Node.AddUniqueDep(Module.GetPeerdirType() == EPT_BuildFrom ? EDT_BuildFrom : EDT_Include, EMNT_Directory, dirEnt.GetElemId());
    FORCE_UNIQ_CONFIGURE_TRACE(dirEnt, H, NEvent::TNeedDirHint(TString{NPath::CutType(dir)}));
}
