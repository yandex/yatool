#include "json_subst.h"

#include "dump_info.h"
#include "exec.h"
#include "json_visitor.h"
#include "macro.h"
#include "macro_processor.h"
#include "module_restorer.h"
#include "shell_subst.h"

#include <devtools/ymake/symbols/symbols.h>

#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/make_plan/make_plan.h>

#include <library/cpp/json/json_reader.h>
#include <library/cpp/json/json_writer.h>

#include <util/generic/algorithm.h>
#include <util/generic/fwd.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/str.h>
#include <util/string/split.h>
#include <util/string/subst.h>
#include <util/system/types.h>
#include <util/generic/overloaded.h>

namespace {

    const char* const JSON_ARRAY_DELIMITER = "\", \"";

    void XXXJsonEmptyList(TString& res) {
        Y_ASSERT(res.empty());
        res = "[]";
    }

    void XXXFinalizeJsonList(TString& res) {
        if (res.empty()) {
            XXXJsonEmptyList(res);
        } else {
            res.reserve(res.size() + 2 + 2);
            res.prepend("[\"");
            res.append("\"]");
            Y_DEBUG_ABORT_UNLESS(NJson::ValidateJson(res), "%s", res.c_str());
        }
    }

    void XXXConcatJsonLists(TString&& tail, TString& addTo) {
        Y_DEBUG_ABORT_UNLESS(NJson::ValidateJson(addTo), "%s", addTo.c_str());
        Y_ASSERT(addTo.back() == ']');
        Y_DEBUG_ABORT_UNLESS(NJson::ValidateJson(tail), "%s", tail.c_str());
        Y_ASSERT(tail.front() == '[');
        if (addTo == "[]") {
            addTo = tail;
        } else if (tail == "[]") {
            // Nothing to do.
        } else {
            addTo.reserve(addTo.size() + tail.size());
            addTo.back() = ',';
            addTo.push_back(' ');
            addTo.append(tail, 1);
            Y_DEBUG_ABORT_UNLESS(NJson::ValidateJson(addTo), "%s", addTo.c_str());
        }
    }

    TString GenerateVecParam(const TYVar* var, const TStringBuf& mname, TCommandInfo& cmdInfo, const TVars& vars) {
        TString res;
        TMacroData macro;
        macro.Name = mname;
        macro.Flags.Set(EMF_QuoteEach);
        macro.Flags.Set(EMF_BreakQuote);
        macro.FormatFor = ECF_Json;
        if (var) {
            cmdInfo.SubstData(nullptr, macro, var, vars, ECF_Json, ESM_DoSubst, res);
        } else {
            cmdInfo.SubstData(nullptr, macro, vars, ECF_Json, ESM_DoSubst, res);
        }
        XXXFinalizeJsonList(res);
        return res;
    }
} // namespace

void TJsonCmdAcceptor::Start(const TStringBuf& cmd, TString& res[[maybe_unused]]) {
    InCmd = cmd;
}

void TJsonCmdAcceptor::ConvertText(TString& res, const TStringBuf& text) {
    // TODO: handle backslash + space case
    if (InToken) {
        TokenStart = text.data() - InCmd.data();
    }
    for (const char& c : text) {
        if (InToken && c == ' ') {
            FinishToken(res, &c, false);
        }

        if (!InToken && c != ' ') {
            TokenStart = &c - InCmd.data();
            InToken = true;
        }
    }
}

void TJsonCmdAcceptor::ConvertMacro(TString& res, TMacroData& macro) {
    if (macro.ComplexArg /*wtf?*/) {
        return;
    }

    //macro.Quoted = true;
    macro.FormatFor = ECF_Json;
    macro.PrependQuoteDelim = !InToken && !ContinueToken;

    if (InToken) {
        FinishToken(res, macro.OrigFragment.data(), true);
    }
    //InToken = true;
}

void TJsonCmdAcceptor::StartCommand(TString& res[[maybe_unused]]) {
    Commands.emplace_back();
    InToken = false;
    ContinueToken = true;
    TopQuote = 0;
}

void TJsonCmdAcceptor::FinishCommand(TString& res) {
    if (InToken) {
        FinishToken(res, InCmd.end(), false);
    }
    auto& curcmd = Commands.back();
    XXXFinalizeJsonList(res);
    curcmd.CmdStr = std::move(res);
    res.clear();
}

void TJsonCmdAcceptor::Finish(TString& res[[maybe_unused]], TCommandInfo& cmdInfo, const TVars& vars) {
    OnCmdFinished(Commands, cmdInfo, vars);
}

//
//
//

void TJsonCmdAcceptor::BeginScript() {
    Commands.clear();
}

void TJsonCmdAcceptor::BeginCommand() {
    Commands.emplace_back();
    Commands.back().CmdStr = TVector<TString>();
}

void TJsonCmdAcceptor::WriteArgument(TStringBuf arg) {
    std::get<TVector<TString>>(Commands.back().CmdStr).push_back(TString(arg));
}

void TJsonCmdAcceptor::WriteCwd(TStringBuf cwd) {
    Commands.back().Cwd = cwd;
}

void TJsonCmdAcceptor::WriteStdout(TStringBuf path) {
    Commands.back().StdOut = path;
}

void TJsonCmdAcceptor::WriteEnv(TStringBuf env) {
    auto envStr = TString(env);

    // HACK:
    // originally `BreakQuotedExec(nextsubst.Name, quoteDelim, false)`;
    // `TCommandInfo::SubstData` does this to env data before adding it to `EnvSetDefs`,
    // which causes backslashes in, e.g., `TOOLCHAIN_ENV`, to be doubled;
    // later in `TSubst2Json::CmdFinished` those are kicked out via `SubstGlobal(valRepl, "\\\\:", ":")`;
    // TODO: what are these backslashes doing there to begin with?
    BreakQuotedExec(envStr, "\", \"", false);

    Commands.back().EnvSetDefs.push_back(envStr);
}

void TJsonCmdAcceptor::WriteResource(TStringBuf uri) {
    auto uriStr = TString(uri);
    Commands.back().ResourceUris.push_back(uriStr);
}

void TJsonCmdAcceptor::WriteTaredOut(TStringBuf path) {
    auto pathStr = TString(path);
    Commands.back().TaredOuts.push_back(pathStr);
}

void TJsonCmdAcceptor::EndCommand() {
}

void TJsonCmdAcceptor::EndScript(TCommandInfo& cmdInfo, const TVars& vars) {
    OnCmdFinished(Commands, cmdInfo, vars);
}

//
//
//

// a b -> "a", "b"
// a $b -> "a", $b
// a$b c -> "a"$b, "c"
// a${b}c -> "a"$b"c"
// "a b" c -> "a b", "c"
// "a $b c" d -> "a $b c", "d"

// JustStarted && c != ' ' -> put '"', start token
// in macro, with !InToken (aka PrependQuoteDelim) -> put '", "'
// we should know that last macro closed the token and set up InToken if it did
// Macro closes the token if it is not Quoted and finishes with <space> or
// macro knows if it was quoted, etc.
// in finish, unless JustStarted -> put '"'

inline void TJsonCmdAcceptor::FinishToken(TString& res, const char* at, bool nextIsMacro) {
    size_t pos = at - InCmd.data();
    TStringBuf token = InCmd.SubStr(TokenStart, pos - TokenStart);

    if (token.size()) {
        if (!ContinueToken) {
            res += JSON_ARRAY_DELIMITER;
        }
        // Perform appropriate escaping for a JSON element
        TString strValue{token};
        TopQuote = BreakQuotedExec(strValue, "", false, TopQuote);
        res += strValue;
        if (TopQuote) {
            TokenStart = pos;
        }
    }
    ContinueToken = nextIsMacro || TopQuote;
    InToken = nextIsMacro || TopQuote;
}

TSubst2Json::TSubst2Json(const TJSONVisitor& vis, TDumpInfoUID& dumpInfo, TMakeNode* makeNode)
    : DumpInfo(dumpInfo)
    , JSONVisitor(vis)
    , MakeNode(makeNode)
{
}

void TSubst2Json::GenerateJsonTargetProperties(const TConstDepNodeRef& node, const TModule* mod, bool isGlobalNode) {
    const auto nodeType = node->NodeType;

    if (IsModuleType(nodeType) || isGlobalNode) {
        Y_ASSERT(mod != nullptr);
        TStringBuf lang = mod->GetLang();
        if (!lang.empty()) {
            TargetProperties["module_lang"] = to_lower(TString{lang});
        }

        TargetProperties["module_dir"] = TString(mod->GetDir().CutType());
    }

    if (IsModuleType(nodeType)) {
        Y_ASSERT(mod != nullptr);
        auto renderModuleType = static_cast<ERenderModuleType>(mod->GetAttrs().RenderModuleType);
        TargetProperties["module_type"] = ToString(renderModuleType);

        TStringBuf tag = mod->GetTag();
        if (mod->IsFromMultimodule() && !tag.empty()) {
            TargetProperties["module_tag"] = to_lower(TString{tag});
        }
    } else if (isGlobalNode) {
        Y_ASSERT(mod != nullptr);
        TargetProperties["module_type"] = ToString(ERenderModuleType::Library);
        if (mod->IsFromMultimodule() && !mod->GetTag().empty()) {
            TargetProperties["module_tag"] = to_lower(TString{mod->GetTag()}) + "_global";
        }
        else {
            TargetProperties["module_tag"] = "global";
        }
    }
}

void TSubst2Json::UpdateInputs() {
    TVars vars;
    TMakeNode& makeNode = *MakeNode;

    DumpInfo.MoveInputsTo(makeNode.Inputs);
}

void TSubst2Json::CmdFinished(const TVector<TSingleCmd>& commands, TCommandInfo& cmdInfo, const TVars& vars) {
    TMakeNode& makeNode = *MakeNode;

    makeNode.Uid = DumpInfo.UID;
    makeNode.SelfUid = DumpInfo.SelfUID;
    makeNode.TargetProps.swap(TargetProperties);
    makeNode.OldEnv["ARCADIA_ROOT_DISTBUILD"] = "$(SOURCE_ROOT)";

    Y_ASSERT(makeNode.Inputs.empty());
    DumpInfo.MoveInputsTo(makeNode.Inputs);

    for (const auto& dep : DumpInfo.Deps) {
        auto nodeIt = JSONVisitor.Nodes.find(dep);
        auto nodeUid = nodeIt->second.GetNodeUid();
        makeNode.Deps.push_back(nodeUid);
    }

    for (const auto& dep : DumpInfo.ToolDeps) {
        auto nodeIt = JSONVisitor.Nodes.find(dep);
        auto nodeUid = nodeIt->second.GetNodeUid();
        makeNode.ToolDeps.push_back(nodeUid);
    }

    if (IsFake) {
        return;
    }

    if (cmdInfo.KV) {
        makeNode.KV.swap(*cmdInfo.KV);
    }
    makeNode.Requirements = cmdInfo.TakeRequirements();
    makeNode.LateOuts = std::move(DumpInfo.LateOuts);
    TYVar lateOutsVars;
    auto cmdLateOuts = cmdInfo.LateOuts.Get();
    lateOutsVars.reserve(
        makeNode.LateOuts.size() +
        (Y_UNLIKELY(cmdLateOuts) ? cmdLateOuts->size() : 0)
    );
    for (const auto& lateOut : makeNode.LateOuts) {
        lateOutsVars.emplace_back(lateOut, false, true);
    }
    if (Y_UNLIKELY(cmdLateOuts)) {
        for (const auto& lateOut : *cmdLateOuts) {
            lateOutsVars.emplace_back(lateOut, false, true);
        }
    }

    XXXJsonEmptyList(makeNode.Outputs);
    XXXConcatJsonLists(GenerateVecParam(nullptr, "OUTPUT", cmdInfo, vars), makeNode.Outputs);
    XXXConcatJsonLists(GenerateVecParam(&DumpInfo.ExtraOutput, "EXTRA_OUTPUT", cmdInfo, vars), makeNode.Outputs);
    XXXConcatJsonLists(GenerateVecParam(&lateOutsVars, "LATE_OUTS", cmdInfo, vars), makeNode.Outputs);

    TVector<TString> resources;
    TVector<TString> taredOuts;
    TSet<TStringBuf> uniqResources;
    TSet<TStringBuf> uniqTaredOuts;
    Y_ASSERT(makeNode.ResourceUris.empty());
    Y_ASSERT(makeNode.TaredOuts.empty());

    for (auto& cmd : commands) {
        makeNode.Cmds.emplace_back();
        TMakeCmd& makeCmd = makeNode.Cmds.back();
        makeCmd.CmdArgs = cmd.CmdStr;
        if (cmd.Cwd.size()) {
            makeCmd.Cwd = cmd.Cwd;
        }
        if (cmd.StdOut.size()) {
            makeCmd.StdOut = cmd.StdOut;
        }
        makeCmd.Env["ARCADIA_ROOT_DISTBUILD"] = "$(SOURCE_ROOT)";
        for (const auto& env : cmd.EnvSetDefs) {
            TStringBuf name, val;
            StringSplitter(env).Split('=').Limit(2).TryCollectInto(&name, &val);
            if (name.empty()) {
                break;
            }

            // XXX ymake keeps escape symbol '\' (which is escaped, that is double '\') in macro strings
            TString valRepl(val);
            SubstGlobal(valRepl, "\\\\:", ":");
            SubstGlobal(valRepl, "\\\\;", ";");

            makeCmd.Env[name] = valRepl;
            makeNode.OldEnv[name] = valRepl;
        }

        for (const auto& uri : cmd.ResourceUris) {
            if (uniqResources.insert(uri).second) {
                resources.push_back(uri);
            }
        }

        for (const auto& out : cmd.TaredOuts) {
            if (uniqTaredOuts.insert(out).second) {
                taredOuts.push_back(out);
            }
        }
    }

    makeNode.ResourceUris.swap(resources);
    makeNode.TaredOuts.swap(taredOuts);
}

void TSubst2Json::OnCmdFinished(const TVector<TSingleCmd>& commands, TCommandInfo& cmdInfo, const TVars& vars) {
    CmdFinished(commands, cmdInfo, vars);
}

void TSubst2Json::FakeFinish(TCommandInfo& cmdInfo) {
    TVars vars;
    IsFake = true;
    TString emptyString;
    GetAcceptor()->Finish(emptyString, cmdInfo, vars);
}
