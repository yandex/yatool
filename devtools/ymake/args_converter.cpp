#include "args_converter.h"

#include <devtools/ymake/common/memory_pool.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/options/static_options.h>

#include <util/generic/algorithm.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>
#include <util/string/ascii.h>

namespace {
    constexpr TStringBuf KnownRoots[] = {
        "$ARCADIA_BUILD_ROOT/"sv,
        "${ARCADIA_BUILD_ROOT}/"sv,
        "$ARCADIA_ROOT/"sv,
        "${ARCADIA_ROOT}/"sv
    };

    bool ignoreArg(TStringBuf arg) {
        return arg.empty() || arg[0] == '$' && AnyOf(KnownRoots, [arg](auto elem) { return arg.StartsWith(elem); });
    }
}

struct TScriptArg {
    TStringBuf Arg;
    size_t Argtype;

    TScriptArg(const TStringBuf& arg, size_t argtype)
        : Arg(arg)
        , Argtype(argtype)
    {
    }
};

struct TTypedArgArray {
    TVector<TStringBuf> Args;
    bool GotKeyword = false;
    size_t AlienArgs = 0;

    size_t NumNativeArgs() const {
        return Args.size() - AlienArgs;
    }
};

struct TTypedArgs : TVector<TTypedArgArray> {
    TTypedArgs(size_t origArgId)
        : OrigArgId(origArgId)
    {
    }

    //user defined array arguments can be without brackets if they are on the last position
    size_t CntUserArgs = 0; //count user defined arguments
    const size_t OrigArgId;
};

static bool IsValidSymbol(char sym) {
    return !(IsAsciiAlpha(sym) || sym == '_' || sym == '-' || sym == NPath::PATH_SEP);
}

static bool IsValidSymbolBefore(char sym) {
    return IsValidSymbol(sym) && sym != '.';;
}

static bool IsValidSymbolAfter(char sym) {
    return IsValidSymbol(sym);
}

//note: we are really need module here only for use string pool. May be pass string pool instead module?
static void FillTypedArgs(const TCmdProperty& cmdProp, const TVector<TStringBuf>& args, TTypedArgs& typedArgs, IMemoryPool& sspool) {
    TVector<TScriptArg> scriptArgs;
    TVector<std::pair<size_t, size_t>> origArgsPos;
    size_t argId = typedArgs.OrigArgId;
    ssize_t argLimit = 0; //0 is for infinity; -1 for none
    bool needDeepReplace = false;
    bool inArgArray = false; //array is one argument
    for (size_t i = 0; i < args.size(); ++i) {
        TString argStr = TString{args[i]};
        if (cmdProp.HasKeyword(argStr)) { //if it is a keyword, designate argLimit and array to put
            const TKeyword& kw = cmdProp.GetKeywords().find(argStr)->second;
            bool useKeyItself = kw.To == 0 && kw.From == 0;

            argLimit = useKeyItself ? -1 : kw.To;
            needDeepReplace = kw.DeepReplaceTo.size();
            argId = cmdProp.Key2ArrayIndex(argStr);
            TTypedArgArray& outArg = typedArgs[argId];

            if (!outArg.GotKeyword && !kw.OnKwPresent.empty()) { // add only once
                outArg.Args.insert(outArg.Args.end(), kw.OnKwPresent.begin(), kw.OnKwPresent.end());
                outArg.AlienArgs = kw.OnKwPresent.size();
            }
            outArg.GotKeyword = true;
        } else {
            if (args[i] == NStaticConf::ARRAY_END || args[i] == NStaticConf::ARRAY_START || argLimit == -1 || (argLimit != 0 && typedArgs[argId].NumNativeArgs() >= size_t(argLimit))) { //cast to max size_t for -1
#ifndef NDEBUG
                YDIAG(VV) << "Use as simple argument: " << args[i] << Endl;
#endif
                argId = typedArgs.OrigArgId;
                argLimit = 0;
                needDeepReplace = false;
            }
            if (needDeepReplace) {
                scriptArgs.push_back(TScriptArg(args[i], argId));
            }
            if (argId == typedArgs.OrigArgId) {
                if (cmdProp.HasUsrArgs()) {
                    auto& origTypedArgs = typedArgs[typedArgs.OrigArgId].Args;
                    origTypedArgs.push_back(args[i]);
                    origArgsPos.emplace_back(i, origTypedArgs.size() - 1);
                }
                if (args[i] == NStaticConf::ARRAY_START)
                    inArgArray = true;
                else if (args[i] == NStaticConf::ARRAY_END)
                    inArgArray = false;
                if (!inArgArray)
                    typedArgs.CntUserArgs++;
            } else {
                typedArgs[argId].Args.push_back(args[i]);
            }
        }
    }

    if (scriptArgs.size() && origArgsPos.size()) {
        std::sort(scriptArgs.begin(), scriptArgs.end(), [](const TScriptArg& arg1, const TScriptArg& arg2) {
                    return arg1.Arg.size() > arg2.Arg.size(); //sort from long to short
        });
        for (TVector<TScriptArg>::iterator it = scriptArgs.begin(); it != scriptArgs.end(); ++it) {
            const TStringBuf scriptArg = it->Arg;
            if (ignoreArg(scriptArg)) {
                continue;
            }
            for (auto pos = origArgsPos.begin(); pos != origArgsPos.end();) {
                const auto& arg = args[pos->first];
                const size_t inpos = arg.find(scriptArg);
                if (inpos != TStringBuf::npos) {
                    bool allowReplaceBefore = !inpos || IsValidSymbolBefore(arg[inpos - 1]);
                    bool allowReplaceAfter = (inpos + scriptArg.size() == arg.size() || IsValidSymbolAfter(arg[inpos + scriptArg.size()]));
                    if (allowReplaceBefore && allowReplaceAfter) {
                        TString mod = cmdProp.GetDeepReplaceTo(it->Argtype);
                        TString v = TString{arg.Head(inpos)} + "${" + mod + "\"" + scriptArg + "\"}" + TString{arg.Tail(inpos + scriptArg.size())};
                        YDIAG(V) << "Replacement: " << arg << "->" << v << Endl;
                        typedArgs[typedArgs.OrigArgId].Args[pos->second] = sspool.Append(v);
                        pos = origArgsPos.erase(pos);
                    } else {
                        ++pos;
                    }
                } else {
                    ++pos;
                }
            }
        }
    }
}

static size_t ConvertTypedArgs(const TCmdProperty& cmdProp, const TVector<TStringBuf>& args, const TTypedArgs& typedArgs, TVector<TStringBuf>& res) {
    size_t firstOrig = 0;
    for (size_t cnt = 0; cnt < typedArgs.size(); cnt++) {
        const TTypedArgArray& outArg = typedArgs[cnt];
        if (cnt != typedArgs.OrigArgId) {
            res.push_back(NStaticConf::ARRAY_START);
        } else {
            firstOrig = res.size();
        }
        for (size_t j = 0; j < outArg.Args.size(); j++) {
            res.push_back(outArg.Args[j]);
        }
        if (cnt != typedArgs.OrigArgId) {
            const TKeyword& kw = cmdProp.GetKeywordData(cnt);
            if (outArg.GotKeyword && outArg.NumNativeArgs() < kw.From)
                YWarn() << "Received only " << outArg.NumNativeArgs() << " args with keyword " << cmdProp.GetKeyword(cnt) << "; must be greater than " << kw.From << ". Args: "<< JoinVectorIntoString(args, " ") << Endl;
            if (!outArg.GotKeyword && !kw.OnKwMissing.empty())
                res.insert(res.end(), kw.OnKwMissing.begin(), kw.OnKwMissing.end());
            res.push_back(NStaticConf::ARRAY_END);
        }
    }

    if (cmdProp.HasUsrArgs() && typedArgs.CntUserArgs < cmdProp.GetNumUsrArgs()) {
        res.push_back(NStaticConf::ARRAY_START);
        res.push_back(NStaticConf::ARRAY_END);
    }
    return firstOrig;
}

size_t ConvertArgsToPositionalArrays(const TCmdProperty& cmdProp, TVector<TStringBuf>& args, IMemoryPool& sspool) {
    TTypedArgs typedArgs(cmdProp.GetKeyArgsNum());
    typedArgs.resize(cmdProp.GetKeyArgsNum() + 1); //last is for Args... without keywords

    FillTypedArgs(cmdProp, args, typedArgs, sspool);
    TVector<TStringBuf> res;
    size_t firstOrig = ConvertTypedArgs(cmdProp, args, typedArgs, res);

    args.swap(res);
    return firstOrig;
}
