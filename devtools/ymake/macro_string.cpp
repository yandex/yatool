#include "macro_string.h"

#include "macro.h"
#include "vars.h"

#include <devtools/ymake/lang/macro_parser.h>

#include <devtools/ymake/options/static_options.h>

#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/diag/trace.ev.pb.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag/manager.h>

#include <util/generic/fwd.h>
#include <util/generic/hash_set.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/output.h>
#include <util/string/cast.h>
#include <util/string/split.h>
#include <util/string/subst.h>
#include <util/system/compiler.h>
#include <util/system/types.h>
#include <util/system/yassert.h>

#include <fmt/format.h>

static const char CmdDelimC = ':';
static const char* const CmdDelimS = ":";
static const char CmdNameDelimC = '=';
static const char* const CmdNameDelimS = "=";

TString FormatCmd(ui64 id, const TStringBuf& name, const TStringBuf& value) {
    return TString::Join(ToString<ui64>(id), CmdDelimS, name, CmdNameDelimS, value);
}

void ParseCmd(const TStringBuf& source, ui64& id, TStringBuf& name, TStringBuf& value) {
    TStringBuf temp = source;
    size_t afterId = temp.find(CmdDelimC);
    size_t afterCmdName = temp.find(CmdNameDelimC, afterId);
    AssertEx(afterId != TStringBuf::npos, TString::Join("ParseCmd: CmdDelim \"", CmdDelimS, "\" not found in ", source));
    AssertEx(afterCmdName != TStringBuf::npos, TString::Join("ParseCmd: CmdNameDelim \"", CmdNameDelimS, "\" not found in ", source));

    id = FromString<ui64>(temp.SubStr(0, afterId++));
    name = temp.SubStr(afterId, afterCmdName - afterId);
    value = temp.SubStr(afterCmdName + 1);
}

ui64 GetId(const TStringBuf& cmd) {
    size_t i = cmd.find(CmdDelimC);
    TStringBuf id = cmd.SubStr(0, i);
    return FromString<ui64>(id);
}

TStringBuf SkipId(const TStringBuf& cmd) {
    TStringBuf pat = cmd;
    size_t pos = pat.find(CmdDelimC);
    return (TStringBuf::npos == pos) ? pat : pat.Skip(pos + 1);
}

TStringBuf GetCmdName(const TStringBuf& cmd) {
    size_t afterId = cmd.find(CmdDelimC);
    size_t afterCmdName = cmd.find(CmdNameDelimC, afterId);
    AssertEx(afterId != TStringBuf::npos, TString::Join("ParseCmd: CmdDelim \"", CmdDelimS, "\" not found in ", cmd));
    AssertEx(afterCmdName != TStringBuf::npos, TString::Join("ParseCmd: CmdNameDelim \"", CmdNameDelimS, "\" not found in ", cmd));
    return cmd.SubStr(afterId + 1, afterCmdName - afterId - 1);
}

TStringBuf CheckAndGetCmdName(const TStringBuf& cmd) {
    size_t afterId = cmd.find(CmdDelimC);
    size_t afterCmdName = cmd.find(CmdNameDelimC, afterId);
    if (afterId == TStringBuf::npos || afterCmdName == TStringBuf::npos) {
        return TStringBuf();
    }
    return cmd.SubStr(afterId + 1, afterCmdName - afterId - 1);
}

TStringBuf GetCmdValue(const TStringBuf& cmd) {
    TStringBuf pat = cmd;
    size_t pos = pat.find(CmdNameDelimC);
    pat.Skip(++pos);
    if (pat.at(0) == ' ') // just for aestetics?
        pat.Skip(1);
    return pat;
}

TString FormatProperty(const TStringBuf& propName, const TStringBuf& value) {
    return TString::Join(propName, CmdNameDelimS, value);
}

TStringBuf GetPropertyName(const TStringBuf& prop) {
    size_t afterCmdName = prop.find(CmdNameDelimC);
    AssertEx(afterCmdName != TStringBuf::npos, TString::Join("ParseProperty: PropertyNameDelim \"", CmdNameDelimS, "\" not found in ", prop));
    return prop.SubStr(0, afterCmdName);
}

TStringBuf GetPropertyValue(const TStringBuf& prop) {
    size_t afterCmdName = prop.find(CmdNameDelimC);
    AssertEx(afterCmdName != TStringBuf::npos, TString::Join("ParseProperty: PropertyNameDelim \"", CmdNameDelimS, "\" not found in ", prop));
    return prop.SubStr(afterCmdName + 1);
}

bool SplitArgs(const TStringBuf& cmd, TVector<TStringBuf>& argNames) {
    size_t varEnd = 0;
    if (cmd.at(0) == '(' && (varEnd = FindMatchingBrace(cmd)) != TString::npos) {
        Split(cmd.SubStr(1, varEnd - 1), ", ", argNames);
        return true;
    }
    return false;
}

bool SplitMacroArgs(const TStringBuf& cmd, TVector<TMacro>& args) {
    if (cmd.at(0) != '(' || cmd.back() != ')') {
        return false;
    }

    TVector<TStringBuf> tmp;
    Split(cmd.SubStr(1, cmd.size() - 2), ", ", tmp);
    args.insert(args.end(), tmp.begin(), tmp.end());
    return true;
}

bool ParseMacroCall(const TStringBuf& macroCall, TStringBuf& name, TVector<TStringBuf>& args) {
    const size_t argsBeginning = macroCall.find('(');
    if (argsBeginning == TStringBuf::npos) {
        name = macroCall;
        return false;
    }

    if (argsBeginning == 0 || macroCall.back() != ')') {
        const TString what = TString::Join("Very strange command. Seems not macro call: ", macroCall);
        TRACE(S, NEvent::TMakeSyntaxError(what, Diag()->Where.back().second));
        YConfErr(Syntax) << what << Endl;
        return false;
    }

    name = macroCall.SubStr(0, argsBeginning);
    const size_t argsEnd = FindMatchingBrace(macroCall, argsBeginning);
    if (argsEnd == TStringBuf::npos) {
        const TString what = TString::Join("Braces mismatch in macro call: ", macroCall);
        TRACE(S, NEvent::TMakeSyntaxError(what, Diag()->Where.back().second));
        YConfErr(Syntax) << what << Endl;
        return false;
    }
    Split(macroCall.SubStr(argsBeginning + 1, argsEnd - argsBeginning - 1), " ", args);
    return true;
}

static void GetOwnArgs(const TStringBuf& pat, THashSet<TStringBuf>& ownVars) {
    TVector<TStringBuf> argNames;
    SplitArgs(pat, argNames);
    for (size_t n = 0; n < argNames.size(); n++) {
        if (argNames[n].EndsWith(NStaticConf::ARRAY_SUFFIX))
            argNames[n].Chop(3);
    }
    ownVars.insert(argNames.begin(), argNames.end());
}

EMacroType GetMacrosFromPattern(const TStringBuf& p, TVector<TMacroData>& macros, bool hasPrefix) {
    TStringBuf pat, cmdName;
    if (hasPrefix) {
        ui64 id;
        ParseCmd(p, id, cmdName, pat);
    } else
        pat = p;

    macros.reserve(100);

    EMacroType mtype = GetMacroType(pat);
    THashSet<TStringBuf> ownVars;
    //SBDIAG << "GM." << mtype << ": " << pat << "\n";
    if (mtype == EMT_MacroDef) {
        GetOwnArgs(pat, ownVars);
    } else if (mtype == EMT_MacroCall && hasPrefix) {
        size_t varEnd = 0;
        if ((varEnd = FindMatchingBrace(pat)) != TString::npos) {
            macros.emplace_back();
            TMacroData& m = macros.back();
            m.Name = cmdName;
            m.HasOwnArgs = true;
            m.SameName = true;
            m.OrigFragment = pat.SubStr(0, varEnd + 1);
            m.Args() = pat.SubStr(0, varEnd + 1); // () included
        }
    }

    size_t n = macros.size();
    GetMacrosFromString(pat, macros, ownVars, cmdName);
    if (mtype == EMT_MacroCall && hasPrefix)
        for (; n < macros.size(); n++)
            macros[n].ComplexArg = true;
    return mtype;
}

bool BreakQuotedEval(TString& substval, const TStringBuf& parDelim, bool allSpace, bool in_quote) {
    size_t copypos = 0;
    size_t copyn = 0;
    TString result;
    bool first_space = true;
    bool space_between = false;
    for (size_t cnt = 0; cnt < substval.size(); cnt++) {
        if (substval[cnt] == '"' && (cnt < 2 || substval[cnt - 1] != '\\' || substval[cnt - 2] != '\\'))
            in_quote = !in_quote;

        if (substval[cnt] != ' ' || in_quote) {
            if (copyn == 0)
                copypos = cnt;
            copyn++;
            space_between = false;
        } else {
            if (copyn > 0) {
                if (!first_space) {
                    result += parDelim;
                } else
                    first_space = false;
                result += TStringBuf(substval).SubStr(copypos, copyn);
                copyn = 0;
            }
            space_between = true;
        }
    }
    if (!space_between && copyn > 0 || space_between && allSpace) {
        if (!first_space)
            result += parDelim;
        result += TStringBuf(substval).SubStr(copypos, copyn);
    }
    substval = result;
    return in_quote;
}

static bool IsValidEscapeSym(const char c) {
    return c == '\'' || c == '"' || c == '\\' || c == '/'; //we do not need here \n, \r, \t, \u, \b, \f - seems that that's all valid escape sequences in json
}

char BreakQuotedExec(TString& substval, const TStringBuf& parDelim, bool allSpace, char topQuote) {
    size_t copypos = 0;
    //size_t copyn = 0;
    bool escaped = false;
    bool inToken = false;
    std::string result;
    bool continueToken = true;
    size_t space_start = 0;
    result.reserve(2 * substval.size());
    for (size_t cnt = 0; cnt < substval.size(); cnt++) {
        /// for exec substval should be separated by spaces.
        /// below special cases with: \s, ", '
        const char c = substval[cnt];
        if (c == '\\') {
            escaped = !escaped;
            if (!inToken) {
                copypos = cnt;
                inToken = true;
            }
            continue;
        }

        if (escaped && !IsValidEscapeSym(c)) {
            if (!continueToken)
                result += parDelim;
            result += TStringBuf(substval).SubStr(copypos, cnt - copypos);
            result += '\\';
            copypos = cnt;
            escaped = false;
            continueToken = true;
            continue;
        }

        if (c == '"' || c == '\'') {
            // NOTE: the truth is, in real shell escaping single quote does not prevent single-quoted string from being closed
            // TODO: this probably must be fixed (by not setting `escaped' flag)
            if ((!topQuote || topQuote == c) && !escaped)
                topQuote ^= c;
            if (!inToken) {
                copypos = cnt + 1;
                inToken = true;
                escaped = false;
                continue;
            }

            // When single or double quote is met,
            // especially when single- or double-quoted string is opened or closed,
            // it is time to perform some backslash-related conversions
            bool add_to_result = c == '\'' && !escaped && topQuote != '"'    // begin or end single-quoted str
                                 || c == '\'' && escaped && topQuote != '\'' // this is unescape_sin
                                 || c == '"' && !escaped && topQuote != '\'' // begin or end double-quoted str
                                 || c == '"' && topQuote == '\'';            // this is to escape the '"'

            if (add_to_result) {
                size_t copyn = cnt - copypos;
                // convert "\'" to "'" unless (WTF?) we are in single-quoted string
                bool unescape_sin = c == '\'' && escaped && topQuote != '\'';
                size_t copy_limit = unescape_sin ? copyn - 1 : copyn;
                TStringBuf suf = c == '"' && topQuote == '\'' ? "\\\"" : unescape_sin ? "'" : "";
                if (!(copy_limit + suf.size())) {
                    copypos = cnt + 1;
                    escaped = false;
                    continue;
                }

                if (!continueToken)
                    result += parDelim;
                if (topQuote == '\'' || c == '\'' && !escaped) {
                    // We are inside single-quoted argv-like string, but our output only uses double quotes.
                    // In shell single-quoted string backslashes are preserved as-is so we have to double them now.
                    TString tmp = TString{TStringBuf(substval).SubStr(copypos, copy_limit)};
                    SubstGlobal(tmp, "\\", "\\\\");
                    result += tmp;
                } else
                    result += TStringBuf(substval).SubStr(copypos, copy_limit);
                result += suf;
                copypos = cnt + 1;
                escaped = false;
                continueToken = true;
                continue;
            }
        }

        // TODO: better use isspace((ui8)c)
        bool at_space = c == ' ' && !(topQuote || escaped);
        escaped = false;

        if (!at_space && !inToken) {
            copypos = cnt;
            inToken = true;
        }

        if (at_space && inToken) {
            size_t copyn = cnt - copypos;
            if (copyn > 0) {
                if (!continueToken)
                    result += parDelim;
                result += TStringBuf(substval).SubStr(copypos, copyn);
            }
            continueToken = false;
            inToken = false;
            space_start = cnt;
        }
    }
    if (inToken || !inToken && allSpace) {
        size_t copyn = substval.size() - (inToken ? copypos : space_start);
        if (copyn) {
            if (!continueToken)
                result += parDelim;
            if (inToken)
                result += TStringBuf(substval).SubStr(copypos, copyn);
        }
    }
    if (escaped) {
        result += '\\';
    }
    substval.MutRef().swap(result);
    return topQuote;
}

static std::string FormatWrongNumberOfArgumentsErr(size_t numberOfFormals, size_t numberOfActuals) {
    Y_ASSERT(numberOfFormals != numberOfActuals);
    return fmt::format("Wrong number of arguments: {} != {}", ToString(numberOfFormals), ToString(numberOfActuals));
}

bool IsPropertyVarName(const TVarStr& var) {
    auto cmdName = CheckAndGetCmdName(var.Name);
    return !cmdName.empty();
}

void TMapMacroVarsErr::Report(TStringBuf argsStr) const {
    switch (ErrorClass) {
        case EMapMacroVarsErrClass::UserSyntaxError: {
            const auto what = TString::Join(Message, "\n\tArgs: ", argsStr);
            TRACE(S, NEvent::TMakeSyntaxError(what, Diag()->Where.back().second));
            YConfErr(Syntax) << what << Endl;
            break;
        }
        case EMapMacroVarsErrClass::ArgsSequenceError:
            YErr() << Message << "\n\tArgs: " << argsStr << Endl;
        break;
    }
}

std::expected<void, TMapMacroVarsErr> MapMacroVars(const TVector<TMacro>& args, const TVector<TStringBuf>& argNames, TVars& vars) {
    if (args.empty() && argNames.empty()) {
        return {};
    }
    bool hasVarArg = argNames[argNames.size() - 1].EndsWith(NStaticConf::ARRAY_SUFFIX); // FIXME: this incorrectly reports "true" for "macro M(X[]){...}" and suchlike
    bool lastMayBeEmpty = argNames.size() - args.size() == 1 && hasVarArg;
    if (argNames.size() != args.size() && (argNames.empty() || (argNames.size() > args.size() && !lastMayBeEmpty))) {
        return std::unexpected(TMapMacroVarsErr{
            .ErrorClass = EMapMacroVarsErrClass::UserSyntaxError,
            .Message = FormatWrongNumberOfArgumentsErr(argNames.size(), args.size())}
        );
    }

    bool isLastArg = false;
    bool isEmptyArray = false;
    bool insideArray = false;
    size_t argNamesIndex = 0;
    for (const auto& arg : args) {
        if (argNamesIndex >= argNames.size() && !hasVarArg) {
            return std::unexpected(TMapMacroVarsErr{
                .ErrorClass = EMapMacroVarsErrClass::UserSyntaxError,
                .Message = FormatWrongNumberOfArgumentsErr(argNames.size(), args.size())}
            );
        }

        isLastArg = argNamesIndex >= argNames.size() - 1;
        TStringBuf name = argNames[isLastArg ? argNames.size() - 1 : argNamesIndex];
        bool isArrayArg = name.EndsWith(NStaticConf::ARRAY_SUFFIX);

        if (!isLastArg && insideArray && !isArrayArg) {
            return std::unexpected(TMapMacroVarsErr{
                .ErrorClass = EMapMacroVarsErrClass::ArgsSequenceError,
                .Message = fmt::format("Passed array but expected element {}", name)}
            );
        }

        if (isArrayArg) {
            name.Chop(3);
        }

        TStringBuf val = arg.Name;
        if (val == "SKIPPED") {
            //SBDIAG << "PMV: Skip: " << name << Endl;
            vars[name]; //leave empty value
            argNamesIndex++;
            continue;
        }

        if (val.StartsWith(NStaticConf::ARRAY_START)) {
            if (insideArray) {
                return std::unexpected(TMapMacroVarsErr{
                    .ErrorClass = EMapMacroVarsErrClass::ArgsSequenceError,
                    .Message = "'[' inside '[ ]'"}
                );
            }
            insideArray = true;
            isEmptyArray = true;
            val.Skip(1);
            if (val.empty()) {
                continue;
            }
        }

        if (val.EndsWith(NStaticConf::ARRAY_END)) {
            if (!insideArray) {
                return std::unexpected(TMapMacroVarsErr{
                    .ErrorClass = EMapMacroVarsErrClass::ArgsSequenceError,
                    .Message = "Expected '[' before ']'"}
                );
            }
            insideArray = false;
            val.Chop(1);
            if (val.empty()) {
                if (isEmptyArray) {
                    vars[name];
                }
                argNamesIndex++;
                continue;
            }
        }

        //if (name == "MnInfo") YDIAG(VV) << vars << "*************\n";
        vars[name].push_back(TVarStr(val, false, false));
        vars[name].back().ImportInhFlags(arg);
        if (IsPropertyVarName(vars[name].back())) {
            vars[name].back().IsMacro = true;
        }
        isEmptyArray = false;
        //SBDIAG << "PMV : " << name << "=" << val << Endl;
        if (!insideArray) {
            argNamesIndex++;
        }
    }

    //allowed for now
    if (argNamesIndex == argNames.size() - 1 && lastMayBeEmpty) {
        TStringBuf name = argNames[argNames.size() - 1];
        //SBDIAG << "Last argument is empty but it as an array argument, so " << name << " may be empty" << Endl;
        name.Chop(3); //was checked before (in lastMayBeEmpty initialization)
        vars[name];
        isLastArg = true;
    }

    if (insideArray) {
        return std::unexpected(TMapMacroVarsErr{
            .ErrorClass = EMapMacroVarsErrClass::ArgsSequenceError,
            .Message = "Expected ']'"}
        );
    }

    if (!isLastArg) {
        return std::unexpected(TMapMacroVarsErr{
            .ErrorClass = EMapMacroVarsErrClass::UserSyntaxError,
            .Message = FormatWrongNumberOfArgumentsErr(argNames.size(), args.size())}
        );
    }

    return {};
}

size_t FindMatchingBrace(const TStringBuf& source, size_t leftPos) noexcept {
    char left = source[leftPos];
    char right = 0;
    switch (left) {
        case '(':
            right = ')';
            break;
        case '[':
            right = ']';
            break;
        case '{':
            right = '}';
            break;
        case '<':
            right = '>';
            break;
        default:
            Y_ASSERT(false && "Opening brace symbol unknown or invalid");
            return TString::npos;
    }
    int level = 0;
    for (size_t i = leftPos + 1; i < source.size(); ++i) {
        if (source[i] == right) {
            if (level == 0) {
                return i;
            }
            --level;
        } else if (source[i] == left) {
            ++level;
        }
    }
    return TString::npos;
}
