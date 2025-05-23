#pragma once

#include "trace_type_enums.h"

#include <devtools/ymake/symbols/file_store.h>

#include <util/generic/vector.h>
#include <util/generic/string.h>
#include <util/generic/strbuf.h>

struct TDiagCtrl {
    struct TWhere {
    public:
        static constexpr const char* TOP_LEVEL = "At top level";

        TWhere() {
            clear();
        }

        void clear() {
            where.clear();
            where.emplace_back(0, TOP_LEVEL);
        }

        void push_back(ui32 ownerElemId, TStringBuf owner) {
            where.emplace_back(ownerElemId, TString(owner));
        }

        void pop_back() {
            where.pop_back();
        }

        const std::pair<ui32, TString>& back() {
            return where.back();
        }

        ui32 size() {
            return where.size();
        }

    private:
        TVector<std::pair<ui32, TString>> where;
    } Where;

    bool Persistency = true;

    union {
        ui32 AllErrors = 0;
        struct { // 3 bits used
            ui32 Syntax : 1;
            ui32 UnkStatm : 1;
            ui32 Misconfiguration : 1;
        };
    };
    union {
        ui32 DbgMsg = 0;
        struct { // 24 bits used
            ui32 MkCmd : 1;
            ui32 NATR : 1;   // TNodeAddCtx trace
            ui32 V : 1;      // verbose messages
            ui32 VV : 1;     // very verbose messages
            ui32 DG : 1;     // TDepGraph stream
            ui32 CVar : 1;   // ya.make-layer variable processing
            ui32 ToDo : 1;   // something not done messages
            ui32 Incl : 1;   // errors of include-file parser
            ui32 LEX : 1;    // diagnostics of lexer.rl6 (ya.make syntax parser)
            ui32 Dev : 1;    // random diagnostics from relatively new code (to be moved to other DbgMsg flags)
            ui32 SUBST : 1;  // macro expansion;
            ui32 Make : 1;   // internal make messages
            ui32 Loop : 1;   // loop detector diagnostics
            ui32 GUpd : 1;   // graph update / EditNode diagnostics
            ui32 Conf : 1;   // confreader.rl6
            ui32 Mkfile : 1; // lexer.rl6 (ya.make parser)
            ui32 FU : 1;     // file updates
            ui32 IPRP : 1;   // intent/induced property propagation
            ui32 IPUR : 1;   // graph rescan for intent/induced
            ui32 PATH : 1;   // ResolveSourcePath/ResolveBuildPath
            ui32 Sln : 1;    // for solution
            ui32 UIDs : 1;   // search for blinking UIDs
            ui32 Star : 1;   // related to star-like subgraphs (multiple outputs)
            ui32 Iter : 1;   // graph iteration
        };
    };
    union {
        ui32 WarningL0d = 0; // default on in debug builds
        struct {         // 2 bits used
            ui32 UnkMod : 1;
            ui32 Garbage : 1;
        };
    };
    union {
        ui32 WarningL0 = 0; // default on
        struct {        // 25 bits used
            ui32 Details : 1;
            ui32 Style : 1;
            ui32 MacroUse : 1;
            ui32 BadFile : 1;
            ui32 UndefVar : 1;
            ui32 BadInput : 1;
            ui32 NoInput : 1;
            ui32 NoOutput : 1;
            ui32 NoCmd : 1;
            ui32 NoSem : 1;
            ui32 UserErr : 1;
            ui32 UserWarn : 1;
            ui32 KnownBug : 1;
            ui32 BadSrc : 1;
            ui32 BadMacro : 1;
            ui32 BadDir : 1;
            ui32 BadAuto : 1;
            ui32 BadIncl : 1;
            ui32 PluginErr : 1;
            ui32 BadOutput : 1;
            ui32 BUID : 1;
            ui32 DupSrc : 1;
            ui32 BadDep : 1;
            ui32 BlckLst : 1;
            ui32 IslPrjs : 1;
        };
    };
    union {
        ui32 WarningL1 = 0; // default off
        struct {        // 11 bits used
            ui32 ShowRecurses : 1;
            ui32 ShowAllLoops : 1;
            ui32 ShowBuildLoops : 1;
            ui32 ShowDirLoops : 1;
            ui32 ChkPeers : 1;
            ui32 ShowAllBadRecurses : 1;
            ui32 NoMain : 1;
            ui32 ChkDepDirExists : 1;
            ui32 PedanticLicenses : 1;
            ui32 GlobalDMViolation : 1;
            ui32 ProxyInDM : 1;
        };
    };
    bool TextLog = true;
    bool BinaryLog = false;
    void Init(const TVector<const char*>& list, bool suppressDbgWarn = false);
    void Init(const TVector<TString>& list, bool suppressDbgWarn = false);
};

TDiagCtrl* Diag();

class TScopedContext {
public:
    explicit TScopedContext(ui32 ownerElemId, TStringBuf owner, bool persistency = true) {
        Diag()->Where.push_back(ownerElemId, owner);
        Diag()->Persistency = persistency;
    }

    explicit TScopedContext(TFileView owner, bool persistency = true)
    : TScopedContext(owner.GetElemId(), owner.GetTargetStr(), persistency)
    {}

    ~TScopedContext() {
        Diag()->Where.pop_back();
        Diag()->Persistency = true;
    }
};
