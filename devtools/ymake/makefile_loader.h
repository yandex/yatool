#pragma once

#include "conf.h"
#include "induced_props.h"
#include "general_parser.h"

#include <devtools/ymake/diag/stats.h>
#include <devtools/ymake/lang/makelists/statement_location.h>
#include <devtools/ymake/lang/eval_context.h>

#include <devtools/libs/yaplatform/platform_map.h>

#include <util/generic/hash_set.h>
#include <util/generic/ptr.h>
#include <util/generic/set.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

class TYMake;
class TModuleDef;
class IContentProvider;
namespace NJson {
    class TJsonValue;
}

class TDirParser: public TEvalContext {
public:
    TDirParser(TYMake& yMake, TFileView dir, TStringBuf makefile, TPropValues& modules, TPropValues& recurses, TPropValues& testRecurses, IContentProvider* provider);
    ~TDirParser() override = default;
    void Load();

    TStringBuf GetMultiModuleName() const {
        return MultiModuleName;
    }

    TStringBuf GetOwners() const;

    const TVector<TString>& GetIncludes() {
        return Includes;
    }

    const TVector<TModuleDef*>& GetModules() {
        return ModulesInDir;
    }

    static bool MessageStatement(const TStringBuf& name, const TVector<TStringBuf>& args, const TVars& vars, const TBuildConfiguration& conf);

    bool ShouldSkip(TStringBuf command) const override;

    void EnterModuleScope();
    void LeaveModuleScope();

protected:
    bool UserStatement(const TStringBuf& name, const TVector<TStringBuf>& args) override;
    TIncludeController OnInclude(TStringBuf incFile, TStringBuf fromFile) override;

private:
    bool ModuleStatement(const TStringBuf& name, TArrayRef<const TStringBuf> args, TVars& vars, TOriginalVars& orig);
    bool DeclStatement(const TStringBuf& name, TArrayRef<const TStringBuf> args, TVars& vars, TOriginalVars& orig);
    bool DirStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
    bool MiscStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
    bool KnownStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
    void DataStatement(const TStringBuf& name, const TVector<TStringBuf>& args);
    bool UserStatementImpl(const TStringBuf& name, const TVector<TStringBuf>& args, bool prohibitInternal);
    void AddSubdir(const TStringBuf& dir, const TStringBuf& name);
    TVector<TStringBuf> PartitionRecurse(TStringBuf name,
                                         const TVector<TStringBuf>& args,
                                         size_t index,
                                         size_t count,
                                         TStringBuf balancingConfig);

    TVector<TString> GetDirsFromArgs(const TStringBuf& statementName,
                                     const TVector<TStringBuf>& args,
                                     std::function<TString (TStringBuf)> dirBuilder);
    TVector<TString> GetRecurseDirs(const TStringBuf& statementName,
                                    const TVector<TStringBuf>& args);
    static void ReportFailOnRecurse(const TVector<TString>& takenRecruseDirs,
                                    const TVector<TString>& ignoredRecurseDirs);
    void SaveModule(TModuleDef* module);
    void ClearModule();
    void NukeModule();
    void ApplyDiscard();

    void CheckModuleSemantics();
    void CheckModuleEnd();
    void ReadMakeFile(const TString& makefile);
    void ReadMakeFile();

    ui64 GetMakefileId() const;

    void RefineSubModules(const TStringBuf& name, const TVector<TStringBuf>& args);
    void SetResourceXXXFromJson(const TStringBuf name, const TVector<TStringBuf>& args);
    TVector<TString> GetResourceUriValue(const TStringBuf fileName);
    TVector<TString> GetResourceMapValue(const TStringBuf fileName);
    NYa::TPlatformMap LoadPlatformMapping(const TStringBuf fileName);
    void ReportPlatformResourceError(const TStringBuf name, const yexception& error, const TStringBuf varName = {});
    bool DeclareExternalsByJson(const TStringBuf& name, const TVector<TStringBuf>& args);
private:
    TYMake& YMake;
    TBuildConfiguration& Conf;
    TSymbols& Names;

    TModuleDef* Module;
    TVector<TModuleDef*> ModulesInDir;
    TString Dir;
    TString Makefile;
    TSet<TString> Owners;
    TVector<TString> Includes;

    //dir node
    TPropValues& Modules;
    TPropValues& Recurses;
    TPropValues& TestRecurses;

    int ModuleCount;

    // Multimodule processing state
    struct TSubModule {
        TStringBuf Tag;
        TStringBuf Name;
    };
    TVector<TSubModule> SubModules;
    THashSet<TString> DefaultTags;

    ui16 NextSubModule;
    TStringBuf MultiModuleName;

    // State for multimodule reparsing
    TStringBuf ModulePath;
    size_t ModulePos;
    bool Reparse = false;
    bool ReparseEnd = false;

    // For GO_TEST_FOR and similar: parse as part of module
    bool ReadModuleContentOnly = false;
    bool ReadModuleContentOnlyDone = false;

    bool Discarded = false;

    class TIncludeInfo {
    private:
        static constexpr const ui16 OUT_OF_MODULE = ~0;
        ui16 FromModule = 0;

        static ui16 CurMod(size_t nextSubmodule, bool reparse) {
            return nextSubmodule + (reparse ? 1 : 0);
        }
    public:
        static constexpr const bool DEFAULT_ONCE = false;
        bool Once = DEFAULT_ONCE;

        TIncludeInfo() = default;

        TIncludeInfo(bool inModule, size_t nextSubmodule, bool reparse)
            : FromModule(inModule ? CurMod(nextSubmodule, reparse): OUT_OF_MODULE)
        {
        }

        // It is expected that this one is called on Include seen before.
        // We assume that position is from some previous visit, not current
        bool Register(bool inModule, size_t nextSubmodule, bool reparse) {
            if (!Once) {
                return true;
            }
            if (FromModule == OUT_OF_MODULE || !inModule || FromModule == CurMod(nextSubmodule, reparse)) {
                return false;
            }
            FromModule = CurMod(nextSubmodule, reparse);
            return true;
        }
    };

    THashMap<TString, TIncludeInfo> UniqIncludes; // Owning container for all includes from ya.make
    TVector<TStringBuf> IncludeStack; // The stack of current active includes for loop control

    IContentProvider* Provider;
};

void CheckNoArgs(const TStringBuf& name, const TVector<TStringBuf>& args);
void CheckNumArgs(const TStringBuf& name, const TVector<TStringBuf>& args, size_t num, const char* descr = "");
void CheckMinArgs(const TStringBuf& name, const TVector<TStringBuf>& args, size_t least, const char* descr = "");
void CheckNumArgs(const TStringBuf& name, const TVector<TStringBuf>& args, size_t least, size_t most);

inline bool IsKnownGarbageStatement(const TStringBuf& name) {
    return name == "EXTRADIR" || name == "SOURCE_GROUP";
}

NJson::TJsonValue ParseBalancingConf(TFileContentHolder& balancingConfContent);

TVector<TStringBuf> Partition(const TVector<TStringBuf>& args, size_t index, size_t count);
TVector<TStringBuf> PartitionWithBalancingConf(const TVector<TStringBuf>& newArgs, const NJson::TJsonValue& balancingConfig, size_t index, size_t count);
