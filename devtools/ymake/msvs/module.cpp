#include "module.h"

#include "command.h"
#include "query.h"

#include <devtools/ymake/add_node_context_inline.h>
#include <devtools/ymake/addincls.h>

namespace NYMake {
    namespace NMsvs {
        namespace {
            inline bool CutExt(TFileView path) {
                TStringBuf str = path.GetTargetStr();
                return str.EndsWith(".exe") || str.EndsWith(".lib") || str.EndsWith(".dll");
            }
        }

        TStringBuf TModuleNode::ShortName() const {
            if (CutExt(Name)) {
                return Name.BasenameWithoutExtension();
            }
            return Name.Basename();
        }

        TStringBuf TModuleNode::LongName() const {
            if (CutExt(Name)) {
                return NPath::CutType(Name.NoExtension());
            }
            return Name.CutType();
        }

        TString TModuleNode::Srcdir() const {
            TStringBuf nameStr = Name.GetTargetStr();
            return Conf.RealPath(NPath::SetType(NPath::Parent(nameStr), NPath::Source));
        }

        TString TModuleNode::Bindir() const {
            TStringBuf nameStr = Name.GetTargetStr();
            return Conf.RealPath(NPath::SetType(NPath::Parent(nameStr), NPath::Build));
            // maybe take target's dir
        }

        TFiles TModuleNode::GlobalObjs() const {
            TFiles globalObjs;
            TNodeIds ids;
            GetTargetGlobalObjects(*this, ids);
            ToSortedData(globalObjs, ids, Conf, Graph());
            return globalObjs;
        }


        TModule::TModule(TYMake& yMake, TNodeId id, TNodeId moduleId, const TNodeIds& extraFiles, bool globalSrcsAsNormal)
            : TModuleNode(yMake, id)
            , ModuleCmd(yMake, &yMake.Conf.CommandConf)
            , SampleFileCmd(yMake, &yMake.Conf.CommandConf)
            , Module(yMake.Modules.Get(Value().ElemId))
            , YMake(yMake)
            , IsGlobalLibModule(id != moduleId)
            , ModuleId(moduleId)
        {
            ModuleCmd.GetFromGraph(id, ModuleId, ECF_Make, nullptr, false, IsGlobalLibModule);

            THashSet<TNodeId> fnodes;
            bool skipGlobals = !IsGlobalLibModule && Module->GetAttrs().UseGlobalCmd;
            CollectModuleSrcs(*this, fnodes, &FileNodesWithBuildCmd, globalSrcsAsNormal ? &FileNodesSrcsGlobal : nullptr, FileFlags, FileOutSuffix, skipGlobals);
            fnodes.insert(extraFiles.begin(), extraFiles.end());
            ToSortedData(Files, fnodes, yMake);
            InitSampleFileCmd();
        }

        TModule::EKind TModule::Kind() const {
            if (IsGlobalLibModule) {
                return K_LIBRARY;
            }
            switch (Value().NodeType) {
                case EMNT_Program:
                    return K_PROGRAM;
                case EMNT_Library:
                    if (Module->IsCompleteTarget()) {
                        return K_DLL;
                    }
                    return K_LIBRARY;
                default:
                    return K_UNKNOWN;
            }
        }

        void TModule::InitSampleFileCmd() {
            // Currently ymake doesn't have notion of a project's common CFLAGS.
            // This code only works when all files in a project have the same CFLAGS/CXXFLAGS
            TNodeId fileId = 0;
            TLangId langId = TModuleIncDirs::BAD_LANG;
            Cxx = CxxFlavor::Unknown;
            for (const TFile& file : Files) {
                if (!FileWithBuildCmd(file) || !file.IsObj()) {
                    continue;
                }
                TCommand objCmd(YMake, file.Id(), ModuleId);
                TString sourceInput = objCmd.SourceInput();
                if (!IsRegularSourcePath(sourceInput)) {
                    continue;
                }
                fileId = file.Id();
                langId = NLanguages::GetLanguageIdByExt(NPath::Extension(sourceInput));

                if (IsCSourcePath(sourceInput)) {
                   Cxx |= CxxFlavor::C;
                } else {
                   Cxx |= CxxFlavor::Cpp;
                }
                if (Cxx == CxxFlavor::Mixed) {
                   break;
                }
            }

            if (langId != TModuleIncDirs::BAD_LANG) {
                IncludeVarName = TModuleIncDirs::GetIncludeVarName(langId);
            }

            SampleFileCmd.GetFromGraph(fileId, ModuleId, ECF_Make);
            SampleFileCmd.CmdInfo.SetAllVarsNeedSubst(true);
        }

        TString TModule::CFlags(EConf conf) {
            // FIXME: To support mixed modules we need apply these flags to separate files
            TString flags = Cxx == CxxFlavor::C ? SampleFileCmd.CmdInfo.SubstMacroDeeply(nullptr, "$CFLAGS", SampleFileCmd.Vars, false)
                                                : SampleFileCmd.CmdInfo.SubstMacroDeeply(nullptr, "$CXXFLAGS", SampleFileCmd.Vars, false);
            flags += Cxx != CxxFlavor::Cpp ? " " + SampleFileCmd.CmdInfo.SubstMacroDeeply(nullptr, "$CONLYFLAGS", SampleFileCmd.Vars, false) : "";
            return ResolveBuildTypeSpec(flags, conf);
        }

        TString TModule::LinkFlags(EConf conf) {
            TString flags = ModuleCmd.CmdInfo.SubstMacroDeeply(nullptr, "$LINK_EXE_FLAGS", ModuleCmd.Vars, false);
            flags = ResolveBuildTypeSpec(flags, conf);
            flags += TString(" ") + ModuleCmd.CmdInfo.SubstMacroDeeply(nullptr, "$LDFLAGS", ModuleCmd.Vars, false);
            flags += TString(" ") + ModuleCmd.CmdInfo.SubstMacroDeeply(nullptr, "$LDFLAGS_GLOBAL", ModuleCmd.Vars, false);
            flags += TString(" ") + ModuleCmd.CmdInfo.SubstMacroDeeply(nullptr, "$EXPORTS_VALUE", ModuleCmd.Vars, false);
            return flags;
        }

        TVector<TString> TModule::LinkStdLibs() {
            TVector<TString> linkStdLibs;
            TString str = ModuleCmd.CmdInfo.SubstMacroDeeply(nullptr, "$LINK_STDLIBS", ModuleCmd.Vars, false);
            TStringBuf s(str);
            for (TStringBuf lib; s.NextTok(' ', lib);) {
                linkStdLibs.push_back(TString{lib});
            }
            return linkStdLibs;
        }

        TString TModule::IncludeString(const TStringBuf& sep) const {
            if (IncludeVarName.empty()) {
                return {};
            }
            TString includeString;
            TStringOutput includeStream(includeString);
            const TYVar& customIncludes = SampleFileCmd.Vars.at(IncludeVarName);
            for (TYVar::const_iterator include = customIncludes.begin(); include != customIncludes.end(); ++include) {
                includeStream << sep << include->Name;
            }
            return includeString;
        }

        TFiles TModule::FilterFiles(const TStringBuf& ext, NPath::ERoot root) const {
            TFiles ret;
            for (const auto& file : Files) {
                if (((root == NPath::Unset) || (root == file.Root())) &&
                    (ext.empty() || ext == file.Ext())) {
                    ret.emplace(file);
                }
            }
            return ret;
        }

        TNodeIds TModule::RetrievePeers() const {
            TNodeIds peers;
            GetTargetPeers(*this, peers);
            return peers;
        }

        bool TModule::FileWithBuildCmd(const TFile& file) const {
            return FileNodesWithBuildCmd.contains(file.Id());
        }

        bool TModule::FileGlobal(TNodeId fileId) const {
            return FileNodesSrcsGlobal.contains(fileId);
        }
    }
}
