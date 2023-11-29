#include "expansion.h"

#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/vars.h> //FIXME: missing PEERDIR

#include <util/system/env.h>

class TVarsLookup : public IMacroValueLookup {
private:
    const TVars& Vars_;

public:
    explicit TVarsLookup(const TVars& vars)
        : Vars_(vars)
    {
    }

    bool Get(const TStringBuf& macroName, const TStringBuf& key, TString& out) const override {
        if (macroName.size()) {
            if (macroName == TStringBuf("ENV")) {
                out = GetEnv(TString(key));
                YDIAG(V) << "reading ENV: " << key << " = " << out << Endl;
                return true;
            } else {
                YConfWarn(CVar) << "unsupported macro reference: " << macroName << Endl;
                return false;
            }
        } else {
            if (NYMake::IsGlobalResource(key)) {
                return false;
            }
            const TYVar* val = Vars_.Lookup(key);
            if (val && (val->DontExpand || val->IsReservedName)) {
                return false;
            } else {
                out = GetCmdValue(Get1(val));
                return true;
            }
        }
    }
};

TString EvalExpr(const TVars& vars, const TStringBuf& expr) {
    return Expand(TVarsLookup(vars), expr);
}

TString EvalExpr(const TVars& vars, const TVector<TStringBuf>& args) {
    return EvalExpr(vars, JoinStrings(args.begin(), args.end(), " "));
}

TString EvalExpr(const TVars& vars, const TVector<TString>& args) {
    return EvalExpr(vars, JoinStrings(args.begin(), args.end(), " "));
}
