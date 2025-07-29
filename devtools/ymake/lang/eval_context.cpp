#include "eval_context.h"

#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/builtin_macro_consts.h>
#include <devtools/ymake/out.h>

#include <library/cpp/case_insensitive_string/case_insensitive_string.h>
#include <util/string/builder.h>
#include <util/string/split.h>
#include <util/string/vector.h>

#include <algorithm>


using TVersion = std::vector<ui32>;

static TVersion ParseVersion(TStringBuf versionAsString) {
    /*
     * Handle both $date-based versions and debian semver-styled versions.
     */
    static constexpr const char* delimeters = ".-";
    /*
     * We assume that there will be no versions longer than 3 semantic elements
     * (thus, both debian semver and $date-$revision versioning schemas could be parsed.
     */
    static constexpr size_t maxElements = 3;

    TVersion version;
    StringSplitter(versionAsString).SplitBySet(delimeters).Take(maxElements).ParseInto(&version);
    return version;
}

const TString TEvalContext::VarExpr(const TStringBuf& var, bool just_parse) {
    // assert var is name
    if (var[0] == '$')
        return !just_parse ? EvalExpr(Vars(), var) : "no";
    return !just_parse ? TString{GetCmdValue(Vars().Get1(var))} : "no";
}

const TString TEvalContext::PrimExpr(TTokStream& args, bool just_parse) {
    /// @todo: support (nested), but that needs lexer
    /// support "values"
    AssertEx(args.size(), "empty subexpression");
    TStringBuf name = args[0];
    // YDIAG(CVar) << "prim expr " << name << Endl;
    args.pop_front();
    return VarExpr(name, just_parse);
}

const TString TEvalContext::UnaryExpr(TTokStream& args, bool just_parse) {
    // YDIAG(CVar) << "unary expr " << SeqDump(args) << Endl;
    AssertEx(args.size(), "empty subexpression");
    if (args[0] == "DEFINED") {
        args.pop_front();
        AssertEx(args.size(), "DEFINED without arg");
        const TStringBuf name = args[0];
        args.pop_front();
        return !just_parse && Vars().Has(name) ? "yes" : "no";
    }
    if (args[0] == "ISNUM") {
        args.pop_front();
        AssertEx(args.size(), "ISNUM without arg");
        const TStringBuf name = args[0];
        TString val = !just_parse && Vars().Has(name) ? VarExpr(name, just_parse) : EvalExpr(Vars(), name);
        args.pop_front();
        long vr = 0;
        return !just_parse && TryFromString<long>(val, vr) ? "yes" : "no";
    }
    return PrimExpr(args, just_parse);
}

bool TEvalContext::BinaryExpr(TTokStream& args, bool just_parse) {
    YDIAG(CVar) << "binary expr " << SeqDump(args) << Endl;
    const TString left = UnaryExpr(args, just_parse);
    if (args.empty()) {
        return !just_parse && NYMake::IsTrue(left);
    }

    if (args.front() == "AND" || args.front() == "OR") {
        /*
         * WARN: this is a crutchy crutch,
         * logical expressions will be handled in another methods.
         *
         * We must not modify args in any way.
         */
        return !just_parse && NYMake::IsTrue(left);
    }

    TStringBuf op = args.front();
    args.pop_front();

    AssertEx(args.size(), "binary op without arg");
    TString right = !just_parse ? EvalExpr(Vars(), args[0]) : "no";
    args.pop_front();

    if (just_parse) {
        // We exctracted all operands up to this point, so parsing is done
        // and we may skip evaluation below regardless of operation used.
        return false;
    }

    // Compare left with right as string
    if (op == "==") {
        return left == right;
    } else if (op == "!=") {
        return left != right;
    } else if (op == "MATCHES") {
        // search for substring case-insensitevely
        return TCaseInsensitiveStringBuf(left).find(right) != TCaseInsensitiveStringBuf::npos;
    } else if (op == "STARTS_WITH") {
        return left.StartsWith(right);
    } else if (op == "ENDS_WITH") {
        return left.EndsWith(right);
    }

    static constexpr TStringBuf VERSION_OP_PREFIX = "VERSION_";
    if (op.starts_with(VERSION_OP_PREFIX) && op.size() == VERSION_OP_PREFIX.size() + 2) {
        TVersion leftAsVersion = ParseVersion(left);
        TVersion rightAsVersion = ParseVersion(right);

        op.remove_prefix(VERSION_OP_PREFIX.size());
        if (op == "GT") {
            return (leftAsVersion > rightAsVersion);
        } else if (op == "GE") {
            return (leftAsVersion >= rightAsVersion);
        } else if (op == "LT") {
            return (leftAsVersion < rightAsVersion);
        } else if (op == "LE") {
            return (leftAsVersion <= rightAsVersion);
        } else {
            ythrow TError() << "Got unsupported operator " << VERSION_OP_PREFIX << op;
        }
    }

    // Compare left with right as number
    long leftAsInt = 0;
    TryFromString<long>(left, leftAsInt);
    long rightAsInt = 0;
    TryFromString<long>(right, rightAsInt);

    if (op == ">") {
        return leftAsInt > rightAsInt;
    } else if (op == ">=") {
        return leftAsInt >= rightAsInt;
    } else if (op == "<") {
        return leftAsInt < rightAsInt;
    } else if (op == "<=") {
        return leftAsInt <= rightAsInt;
    } else {
        ythrow TError() << "Got unsupported operator " << op;
    }
}

bool TEvalContext::NotExpr(TTokStream& args, bool just_parse) {
    AssertEx(args.size(), "empty subexpression");
    if (args[0] == "NOT") {
        args.pop_front();
        AssertEx(args.size(), "NOT without arg");
        bool r = NotExpr(args, just_parse);
        return !just_parse && !r;
    }
    return BinaryExpr(args, just_parse);
}

bool TEvalContext::AndExpr(TTokStream& args, bool just_parse) {
    // YDIAG(CVar) << "and expr " << SeqDump(args) << Endl;
    bool r = NotExpr(args, just_parse);
    if (args.size() && args[0] == "AND") {
        args.pop_front();
        bool r2 = AndExpr(args, !r || just_parse);
        return !just_parse && (r && r2);
    }
    return r;
}

bool TEvalContext::OrExpr(TTokStream& args, bool just_parse) {
    bool r = AndExpr(args, just_parse);
    if (args.size()) {
        AssertEx(args.size() > 1 && args[0] == "OR", "waiting OR instead of " << args[0]);
        args.pop_front();
        bool r2 = OrExpr(args, r || just_parse);
        return !just_parse && (r || r2);
    }
    return r;
}

bool TEvalContext::BoolExpr(const TVector<TStringBuf>& args, bool just_parse) {
    try {
        AssertEx(args.size(), "missing boolean expr");
        // optimize simple case
        if (args.size() == 1)
            return !just_parse && NYMake::IsTrue(VarExpr(args[0], just_parse));
        TDeque<TStringBuf> copy(args.begin(), args.end());
        // TVector<TString> copy(args);
        // if (copy[0] == "NOT") {
        //    copy.erase(copy.begin());
        //    return !BoolExpr(copy);
        // }
        return OrExpr(copy, just_parse);
    } catch (TRuntimeAssertion& e) {
        YDIAG(CVar) << "syntax error in bool expression: (" << TVecDumpSb(args) << ") -- " << e.what() << Endl;
        // ignore all logic errors
        return false;
    }
}

/// derererence all variables in array and reconstruct array splitting by whitespace
void TEvalContext::Deref(TVector<TStringBuf>& args, TSSPool& pool) {
    // substitute variables
    TVector<TStringBuf> res;
    for (TVector<TStringBuf>::iterator i = args.begin(); i != args.end(); ++i) {
        if (i->find('$') != TString::npos) {
#ifndef NDEBUG
            TStringBuf expr = *i;
#endif
            TString v = EvalExpr(Vars(), *i);
            *i = pool.Append(v);
#ifndef NDEBUG
            YDIAG(V) << "Deref: " << expr << " -> " << *i << "\n";
            if (i->empty()) {
                YConfWarn(CVar) << "empty array entry: " << expr << Endl;
            }
#endif
            if (i->find(' ') != TString::npos) {
                TVector<TStringBuf> split;
                Split(*i, " ", split);
                res.insert(res.end(), split.begin(), split.end());
            } else if (i->size()) // skip empty
                res.push_back(*i);
        } else
            res.push_back(*i); //all original args should not be dereferenced
    }
    args.swap(res);
}

// perform escaping of arguments to macro calls in order to avoid the mess related to reserved
// keywords (such as '&&' and ';' - command separators) during processing of macro commands
void TEvalContext::EscapeArgs(TVector<TStringBuf>& args, TSSPool& /* currently unsed */) {
    for (auto& arg : args) {
        if (arg == "&&") {
            arg = TStringBuf("${quo:\"&&\"}");
        } else if (arg == ";") {
            arg = TStringBuf("${quo:\";\"}");
        }
    }
}

void TEvalContext::SetCurrentLocation(const TStringBuf statement, const TStatementLocation& location) {
    StatementContext[statement.data()] = location;
    CurrentLocation = &StatementContext[statement.data()];
}

size_t TEvalContext::GetStatementRow(const TStringBuf statement) {
    const auto it = StatementContext.find(statement.data());
    return it ? it->second.Row : 0;
}

size_t TEvalContext::GetStatementColumn(const TStringBuf statement) {
    const auto it = StatementContext.find(statement.data());
    return it ? it->second.Column : 0;
}

bool TEvalContext::IsCondStatement(TStringBuf command) const {
    return command == NMacro::IF || command == NMacro::ELSE || command == NMacro::ELSEIF || command == NMacro::ENDIF;
}

bool TEvalContext::ShouldSkip(TStringBuf command) const {
    return !BranchTaken() && !IsCondStatement(command);
}

bool TEvalContext::CondStatement(const TStringBuf& name, TVector<TStringBuf>& args, TSSPool& pool) {
    // support nested skipped ifs
    if (!BranchTaken()) {
        do {
            if (name == NMacro::IF)
                ++SkipIfs;
            else if (name == NMacro::ENDIF) {
                if (SkipIfs)
                    --SkipIfs;
                else
                    break;
            } else if (SkipIfs == 0 && (name == NMacro::ELSEIF || name == NMacro::ELSE))
                break;
            return true; // successfully skipped
        } while (false);
    }

    if (name == NMacro::IF) {
        if (BoolExpr(args, false))
            IfState.push_back(IfProcessed);
        else
            IfState.push_back(IfProcessing);
    } else if (name == NMacro::ELSEIF) {
        AssertEx(IfState.size(), "ELSEIF without IF");
        AssertEx(IfState.back() != IfProcessedByElse && IfState.back() != IfSkipedElse, "ELSEIF after ELSE");
        if (IfState.back() == IfProcessing) {
            if (BoolExpr(args, false))
                IfState.back() = IfProcessed;
        } else {
            BoolExpr(args, true); // parse only bool expression
            if (IfState.back() == IfProcessed)
                IfState.back() = IfSkipedElseIf;
            // ignore IfSkipedElseIf
        }
    } else if (name == NMacro::ELSE) {
        AssertEx(IfState.size(), "ELSE without IF");
        AssertEx(IfState.back() != IfProcessedByElse && IfState.back() != IfSkipedElse, "ELSE after ELSE");
        if (IfState.back() == IfProcessed || IfState.back() == IfSkipedElseIf)
            IfState.back() = IfSkipedElse;
        else if (IfState.back() == IfProcessing)
            IfState.back() = IfProcessedByElse;
    } else if (name == NMacro::ENDIF) {
        AssertEx(IfState.size(), "ENDIF without IF");
        IfState.pop_back();
    } else if (BranchTaken()) {
        return VarStatement(name, args, pool);
    }
    return true; // cond command or skipped by IfState
}

bool TEvalContext::SetStatement(const TStringBuf& name, const TVector<TStringBuf>& args, TVars& vars, TOriginalVars& orig) {
    if (name == NMacro::SET) {
        AssertEx(args.size(), "need var name");
        if (args.size() > 1)
            vars.SetStoreOriginals(args[0], EvalExpr(vars, JoinStrings(args.begin() + 1, args.end(), " ")), orig);
        else
            vars.SetStoreOriginals(args[0], "", orig);
        //Check if there is a condition for args[0]
    } else if (name == NMacro::SET_APPEND) {
        if (args.empty()) {
            AssertEx(!args.empty(), "need var name");
        } else {
            vars.SetAppendStoreOriginals(args[0], EvalExpr(vars, JoinStrings(args.begin() + 1, args.end(), " ")), orig);
        }
    } else if (name == NMacro::SET_APPEND_WITH_GLOBAL) {
        vars.SetAppendWithGl(args, orig);
    } else if (name == NMacro::DEFAULT) {
        // AssertEx(+args == 2);
        AssertEx(args.size(), "need var name");
        if (vars.Get1(args[0]).empty())
            vars.SetStoreOriginals(args[0], EvalExpr(vars, JoinStrings(args.begin() + 1, args.end(), " ")), orig);
    } else if (name == NMacro::ENABLE) {
        AssertEx(args.size(), "need var name");
        vars.SetStoreOriginals(args[0], "yes", orig);
    } else if (name == NMacro::DISABLE) {
        AssertEx(args.size(), "need var name");
        vars.SetStoreOriginals(args[0], "no", orig);
        //Check if there is a condition for args[0]==false
    } else
        return false;
    return true;
}

bool TEvalContext::VarStatement(const TStringBuf& name, TVector<TStringBuf>& args, TSSPool& pool) {
    if (SetStatement(name, args, Vars(), OrigVars())) {
        //recalc variables
        YDIAG(V) << "Need to recalc conditions with " << args[0] << Endl;
        Condition.RecalcVars(TString::Join("$", args[0]), Vars(), OrigVars());
    } else {
        Deref(args, pool);
        EscapeArgs(args, pool);
        UserStatement(name, args);
    }
    return true;
}
