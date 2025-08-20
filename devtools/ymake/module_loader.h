#pragma once

#include "module_state.h"

#include "macro_processor.h"
#include "general_parser.h" // EMakeNodeType
#include "vars.h"
#include "conf.h"

#include <devtools/ymake/common/memory_pool.h>
#include <devtools/ymake/common/split_string.h>
#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/symbols/globs.h>

#include <util/generic/vector.h>
#include <util/generic/string.h>
#include <util/generic/map.h>

class TYMake;
class TModules;

struct TModuleGlobInfo {
    ui32 GlobId;
    ui32 GlobHash;
    TVector<ui32> WatchedDirs;
    TVector<ui32> MatchedFiles;
    TVector<ui32> Excludes;
    ui32 ReferencedByVar;
};

/// @brief Encapsulate module definition in ya.make
/// This class loads module from ya.make and performs vars evaluation during loading.
class TModuleDef : private TNonCopyable {
private:
    using TPrioStatement = std::pair<size_t, TStringBuf> ;

    struct TMakeFileMap: public TMultiMap<TPrioStatement, TVector<TStringBuf>> {
        mutable TAutoPtr<IMemoryPool> Pool = IMemoryPool::Construct();

        void Add(size_t prio, const TStringBuf& name, TArrayRef<const TStringBuf> args, bool multi) {
            prio = (prio << 24) + (multi ? size() : 0);
            TPrioStatement key(prio, Pool->Append(name));
            iterator i;
            if (multi || (i = find(key)) == end()) {
                i = insert(std::make_pair(key, TVector<TStringBuf>()));
            }
            TVector<TStringBuf>& dstArgs = i->second;
            size_t oldSize = dstArgs.size();
            dstArgs.resize(args.size() + oldSize);
            for (size_t n = 0; n < args.size(); n++) {
                dstArgs[oldSize + n] = Pool->Append(args[n]);
            }
        }

        void Print() const {
            for (const auto& stmt: *this) {
                YDIAG(DG) << "Key: " << stmt.first.first << " " << stmt.first.second << Endl;
                for (const auto& arg: stmt.second) {
                    YDIAG(DG) << arg << Endl;
                }
            }
        }
    };

    TYMake& YMake;
    TBuildConfiguration& Conf;
    TSymbols& Names;
    TModules& Modules;

    TModule& Module;
    TMakeFileMap MakeFileMap;
    const TModuleConf& ModuleConf;
    TVector<TModuleGlobInfo> ModuleGlobs;

    TVars& Vars;
    TOriginalVars OrigVars;
    const TStringBuf Makefile;              // Reference to Module.Makefile
    TUniqVector<TString> MakelistPeers;  // References to some Module.Peers
    bool NeverCache = false;
    bool HasVersion = false;
    bool LateConfErrNoSem_ = false;

    bool IsMacroAllowed(const TStringBuf& name) {
        return ModuleConf.Allowed.contains(name) || !ModuleConf.Restricted.contains(name);
    }

    bool IsMacroIgnored(const TStringBuf& name) {
        return ModuleConf.Ignored.contains(name);
    }

    bool IsMacroAllowedInLintersMake(const TStringBuf& name) {
        return Conf.BlockData.find(name)->second.CmdProps->IsAllowedInLintersMake();
    }

    size_t StatementPriority(const TStringBuf& s);
    void InitModuleSpecConditions();

    using TMacroCall = TCmdProperty::TMacroCall;
    using TMacroCalls = TCmdProperty::TMacroCalls;

    /// @brief construct module filename from REALPRJNAME, PREFIX and SUFFIX and set it to Module
    void InitFromConf();

    /// Maps macro argument values to their names and retrieves macros called
    /// @param args values of macro arguments produced by parser
    /// @param locals Vars where argument values associated with parameter names
    /// @return pointer to vector of macro calls or null if macro is unknown or doesn't have calls
    const TMacroCalls* PrepareMacroBody(const TStringBuf& name, TArrayRef<const TStringBuf> args, TVars& locals);

    /// Substitutes macro arguments and computes macro name via specialization
    /// @param macroCall is single macro call as recoded in CmdProps
    /// @param locals values for substitution
    /// @param callArgs arguments for a call
    /// @param name caller's name for diagnostic purposes
    /// @return specialized name and array of arguments via callArgs
    TStringBuf PrepareMacroCall(const TMacroCall& macroCall, const TVars& locals, TSplitString& callArgs, const TStringBuf& name);

    /// Executes SET macros and updates triggers
    /// return true if macro was handled false otherwise
    bool ProcessBaseMacro(const TStringBuf& macroName, const TVector<TStringBuf>& args, const TStringBuf& name);

    template <typename TMacroHandler>
    void ProcessConfigMacroCalls(const TStringBuf& name, TArrayRef<const TStringBuf> args, TMacroHandler&& handler, TVector<TStringBuf>& callStack, bool lintersMake = false) {
        if (IsMacroIgnored(name)) {
            return;
        }

        if (lintersMake && !IsMacroAllowedInLintersMake(name)) {
            YConfErr(Misconfiguration) << name << " is not allowed in linters.make.inc!" << Endl;
            return;
        }

        TVars localVars(&Vars);
        localVars.Id = Vars.Id;
        const TMacroCalls* macroCalls = PrepareMacroBody(name, args, localVars);
        if (!macroCalls) {
            return;
        }

        ProcessBodyMacroCalls(name, *macroCalls, localVars, std::forward<TMacroHandler>(handler), callStack);
    }

    template <typename TMacroHandler>
    void ProcessBodyMacroCalls(TStringBuf name, const TMacroCalls& macroCalls, TVars localVars, TMacroHandler&& handler, TVector<TStringBuf>& callStack) {
        for (const auto& call: macroCalls) {
            TSplitString callArgs;
            TStringBuf macroName = PrepareMacroCall(call, localVars, callArgs, name);
            if (!ProcessBaseMacro(macroName, static_cast<const TVector<TStringBuf>&>(callArgs), name)) {
                handler(macroName, static_cast<const TVector<TStringBuf>&>(callArgs));
                callStack.push_back(macroName);
                AssertEx(callStack.size() < NStaticConf::CALL_STACK_SIZE, "Config error: it is a recursion in macro calls stack: " << TVecDumpSb(callStack));
                ProcessConfigMacroCalls(macroName, static_cast<const TVector<TStringBuf>&>(callArgs), handler, callStack);
                callStack.pop_back();
            }
        }
    }

public:
    TModuleDef(TYMake& yMake, TModule& module, const TModuleConf& conf);

    /// Complete module initialization
    void InitModule(const TStringBuf& name, TArrayRef<const TStringBuf> args, TVars& vars, TOriginalVars& orig);

    // Called on user-specified statements only(statements from makelist)
    void ProcessMakelistStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
    bool AddStatement(const TStringBuf& name, TArrayRef<const TStringBuf> args);
    void SetSpecServiceVars(const TString& specVar);

    bool IsInput(const TStringBuf& sname, const TStringBuf& name) const;
    bool IsGlobalInput(const TStringBuf& name) const;
    bool IsUserMacro(const TStringBuf& name) const;
    bool IsMulti(const TStringBuf& name) const; //not merge all calls in one
    bool IsNeverCache() const noexcept {return NeverCache;}
    bool IsVersionSet() const noexcept {return HasVersion;}
    void VersionSet(bool val) noexcept {HasVersion = val;}
    void SetLateConfErrNoSem() { LateConfErrNoSem_ = true; }

    template <typename TMacroHandler>
    void ProcessConfigMacroCalls(const TStringBuf& name, TArrayRef<const TStringBuf> args, TMacroHandler handler, bool lintersMake = false) {
        TVector<TStringBuf> callStack;
        ProcessConfigMacroCalls(name, args, handler, callStack, lintersMake);
    }

    void ProcessModuleMacroCalls(const TStringBuf& name, TArrayRef<const TStringBuf> args, bool lintersMake = false) {
        ProcessConfigMacroCalls(name, args, [this](const TStringBuf& name, TArrayRef<const TStringBuf> args){this->AddStatement(name, args);}, lintersMake);
    }

    void ProcessModuleCall(const TStringBuf& name, TArrayRef<const TStringBuf> args) {
        ModuleConf.ParseModuleArgs(&Module, args);
        ModuleConf.SetModuleBasename(&Module);

        auto pi = Conf.BlockData.find(name);
        if (!pi || !pi->second.CmdProps || !pi->second.CmdProps->HasMacroCalls()) {
            return;
        }

        TVars localVars(&Vars);
        localVars.Id = Vars.Id;
        TVector<TStringBuf> callStack;
        ProcessBodyMacroCalls(
            name,
            pi->second.CmdProps->GetMacroCalls(),
            localVars,
            [this](const TStringBuf& name, TArrayRef<const TStringBuf> args){this->AddStatement(name, args);},
            callStack
        );
    }

    static TGlobRestrictions ParseGlobRestrictions(const TArrayRef<const TStringBuf>& restrictions, const TStringBuf& macro);

    bool ProcessGlobStatement(const TStringBuf& name, const TVector<TStringBuf>& args, TVars& vars, TOriginalVars& orig, std::pair<size_t, size_t> location = {0, 0});
    bool IsExtendGlobRestriction() const;

    TFileView GetName() const {
        return Module.GetName();
    }

    const TModuleConf& GetModuleConf() const {
        return ModuleConf;
    }

    const TBuildConfiguration& GetBuildConf() const {
        return Conf;
    }

    TModule& GetModule() {
        return Module;
    }

    const TModule& GetModule() const {
        return Module;
    }

    TOriginalVars& GetOrigVars() {
        return OrigVars;
    }

    const TOriginalVars& GetOrigVars() const {
        return OrigVars;
    }

    TVars& GetVars() {
        return Vars;
    }

    const TVars& GetVars() const {
        return Vars;
    }

    const TVector<TModuleGlobInfo>& GetModuleGlobs() const;

    void PrintMakeFileMap() const {
        MakeFileMap.Print();
    }

    const TMakeFileMap& GetMakeFileMap() const {
        return MakeFileMap;
    }

    bool IsMakelistPeer(const TStringBuf dir) const {
        return MakelistPeers.has(dir);
    }

    TDepsCacheId Commit();

    static void SetGlobRestrictionsVars(TVars& vars, const TGlobRestrictions& globRestrictions, const ui32 varElemId);
};
