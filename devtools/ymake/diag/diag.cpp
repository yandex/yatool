#include "diag.h"
#include "display.h"

#include <devtools/ymake/context_executor.h>

#include <util/stream/output.h>
#include <util/string/split.h>
#include <util/system/env.h>

void TDiagCtrl::Init(const TVector<const char*>& list, bool suppressDbgWarn) {
    TVector<TString> args;
    for (const auto& item : list) {
        args.push_back(item);
    }
    return Init(args, suppressDbgWarn);
}

void TDiagCtrl::Init(const TVector<TString>& list, bool suppressDbgWarn) {
    AllErrors = WarningL0 = (ui32)-1;
    WarningL0d = 0;
    Y_IF_DEBUG(WarningL0d = (ui32)-1);
    DbgMsg = WarningL1 = 0;

    if (!list.empty() && list[0][0] == '?') {
        Cout << "You can turn on and off individual or group flags in -W\n"
                " * to turn it on, specify flag name\n"
                " * to turn it off, specify (minus) -flag name\n"
                "Separate them by comma, w/o spaces, or use multiple -W's.\n"
                "e.g. -Wignored-tokens,-err\n"
                "List of flags: \n"
                #define DOpt(name, desc, var) "  " name " " desc "\n"
                #include "../diag_options.inc"
                ;
        exit(1);
    }
    for (auto& listEl : list) {
        TVector<TStringBuf> flags;
        Split(TStringBuf(listEl), ",", flags);
        for (size_t n = 0; n < flags.size(); n++) {
            TStringBuf flag = flags[n];
            ui32 on = (ui32)-1;
            if (flag.at(0) == '-') {
                flag.Skip(1);
                on = 0;
            }
#define DOpt(name, desc, var) \
    if (flag == name)         \
        var = on;             \
    else
#include "../diag_options.inc"
                /*else*/
                errx(1, "Unknown flag in -W: %.*s", (int)flag.size(), flag.data());
        }
    }
    Y_UNUSED(suppressDbgWarn);
#if !defined(YMAKE_DEBUG)
    if (DbgMsg && !suppressDbgWarn)
        YWarn() << "-W: some of the dbg flags will be ignored in release build of ymake" << Endl;
#endif
}

TDiagCtrl* Diag() {
    auto ctx = CurrentContext<TExecContext>;
    if (ctx && ctx->DiagCtrl) {
        return ctx->DiagCtrl.get();
    }
    return Singleton<TDiagCtrl>();
}
