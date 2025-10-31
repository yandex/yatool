#pragma once

#include "macro_processor.h"
#include "general_parser.h" // EMakeNodeType
#include "vars.h"
#include "module_dir.h"
#include "module_state.h"
#include "module_loader.h"
#include "module_wrapper.h"

#include <devtools/ymake/all_srcs/all_srcs_context.h>
#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/dbg.h>

#include <util/generic/deque.h>
#include <util/generic/vector.h>
#include <util/generic/string.h>
#include <util/generic/hash_set.h>

class TCommands;

struct TInducedDeps {
    TString Type;
    TVector<TResolveFile> Files;
};

struct TBuildContext {
    const TBuildConfiguration& Conf;
    TDepGraph& Graph;
    TUpdIter& UpdIter;
    TCommands& Commands;
};

bool IsForbiddenStatement(const TStringBuf& name);

/// @brief This builds graph for a module
class TModuleBuilder : public TModuleDirBuilder, public TModuleWrapper
{
public:
    TModuleDef* ModuleDef;
    TVars& Vars;
    TDeps IncludeOnly;
    TDeque<TAutoPtr<TCommandInfo>> CmdAddQueue;
    ui64 CurrentInputGroup = 0; //module input deps can be separated into different groups: for now implicit and explicit
    ui64 CurrentGlobalInputGroup = 0;
    ui64 ModuleNodeElemId = 0;
    ui64 GlobalNodeElemId = 0;
    using TModuleDirBuilder::Module;
    using TModuleDirBuilder::Graph;

    TVector<TInducedDeps> DelayedInducedDeps;
    TVector<std::pair<ui32, TAutoPtr<TCommandInfo>>> FileGroupCmds;
    THashMap<ui32, TVector<TString>> DartIdToGroupVars;

    friend class TAllSrcsContext;
    TAllSrcsContext AllSrcs;

    bool HasBuildFrom = false;

    // Implementation of TPluginUnit
    void CallMacro(TStringBuf name, const TVector<TStringBuf>& args) override;
    void CallMacro(TStringBuf name, const TVector<TStringBuf>& args, TVars extraVars) override;

    void SetProperty(TStringBuf propName, TStringBuf value) override;

    void AddDart(TStringBuf dartName, TStringBuf dartValue, const TVector<TStringBuf>& vars) override;

    TModuleBuilder(TModuleDef& moduleDef, TAddDepAdaptor& node, TBuildContext ctx, const TParsersCache& parsersCache)
       : TModuleDirBuilder(moduleDef.GetModule(), node, ctx.Graph, ctx.Conf.ShouldReportMissingAddincls())
       , TModuleWrapper(moduleDef.GetModule(), ctx.Conf,
                        MakeModuleResolveContext(moduleDef.GetModule(), ctx.Conf, ctx.Graph, ctx.UpdIter, parsersCache))
       , ModuleDef(&moduleDef)
       , Vars(Module.Vars)
       , UpdIter(ctx.UpdIter)
       , Commands{ctx.Commands}
    {
        Y_ASSERT(Module.GetId() == node.ElemId);
        ModuleNodeElemId = Node.ElemId;
    }

    ~TModuleBuilder() {
    }

    TAddDepAdaptor& GetNode() {
        return Node;
    }

    const TAddDepAdaptor& GetNode() const {
        return Node;
    }

    TModule& GetModule() {
        return Module;
    }

    const TModule& GetModule() const {
        return Module;
    }

    const TModuleConf& GetModuleConf() const {
        AssertEx(ModuleDef != nullptr, "Module configuration is unknown");
        return ModuleDef->GetModuleConf();
    }

    // Construction interfaces

    /// @brief Run through preloaded macroses and process them one-by-one
    bool ProcessMakeFile();

    /// @brief Add module output node
    TAddDepAdaptor& AddOutput(ui64 fileId, EMakeNodeType defaultType, bool addToOwn = true) {
        return Node.AddOutput(fileId, defaultType, addToOwn);
    }

    /// @brief Add file dependency to module or global lib
    void AddDep(TVarStrEx& curSrc, TAddDepAdaptor& node, bool isInput, ui64 groupId = 0);

    /// @brief Add file dependency to files group var
    void AddInputVarDep(TVarStrEx& curSrc, TAddDepAdaptor& node);

    /// @brief Recursively collect inputs from macros
    void RecursiveAddInputs();

    /// @brief True if ALL_SRCS node will be added to module
    bool ShouldAddAllSrcs() {
        return Module.GetAttrs().UseAllSrcs;
    }

    void SaveInputResolution(const TVarStrEx& input, TStringBuf origInput, TFileView curDir);

    /// @brief Apply macro call processing as during ya.make load but with immediate
    ///        macro processing instead of call caching.
    void ProcessConfigMacroCalls(const TStringBuf& name, const TVector<TStringBuf>& args) {
        AssertEx(ModuleDef != nullptr, "Cannot process macros: module makefile is not set");
        if (IsForbiddenStatement(name)) {
            return;
        }
        ModuleDef->ProcessConfigMacroCalls(name, args,
            [this](const TStringBuf& name, const TVector<TStringBuf>& args) {
                this->ProcessStatement(name, args);
            });
    }

    /// @brief Process directory macros: PEERDIR, ADDINCL, SRCDIR
    bool DirStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
private:
    enum class EModuleCmdKind {
        // change this enum with caution
        Default = 0,
        Fail, // TODO? drop it
        Global
    };

    TUpdIter& UpdIter;
    TCommands& Commands;

    TOriginalVars& OrigVars() {
         Y_ASSERT(ModuleDef);
         return ModuleDef->GetOrigVars();
    }

    const TOriginalVars& OrigVars() const  {
         Y_ASSERT(ModuleDef);
         return ModuleDef->GetOrigVars();
    }

    // These return true
    bool AddByExt(const TStringBuf& sname, TVarStrEx& src, const TVector<TStringBuf>* args);
    bool AddSource(const TStringBuf& sname, TVarStrEx& src, const TVector<TStringBuf>* args);
    void AddPluginCustomCmd(TMacroCmd& macroCmd);

    void ApplyVarAsMacro(const TStringBuf& name, bool force = false);
    void AddLinkDep(TFileView name, const TString& command, TAddDepAdaptor& node, EModuleCmdKind cmdKind = EModuleCmdKind::Default);
    void AddGlobalVarDep(const TStringBuf& varName, TAddDepAdaptor& node, bool structCmd);
    void AddGlobalVarDeps(TAddDepAdaptor& node, bool structCmd);
    void AddGlobalDep();
    void AddFileGroupVars();
    void AddDartsVars();

    void TryProcessStatement(const TStringBuf& name, const TVector<TStringBuf>& args); // try-catch for ProcessStatement
    void ProcessStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
    bool SrcStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
    bool RememberStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
    bool MacroToVarStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
    bool GenStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
    bool SkipStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
    bool PluginStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
    bool LateGlobStatement(const TStringBuf& name, const TVector<TStringBuf>& args);

    bool QueueCommandOutputs(TCommandInfo& cmdInfo);

    THashSet<ui64, TIdentity> VarMacroApplied;
    TAddDepAdaptor* GlobalNode = nullptr;
    bool GlobalSrcsAreAdded = false;
};
