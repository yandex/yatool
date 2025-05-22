#include "shell_subst.h"

#include "exec.h"
#include "vars.h"
#include "conf.h"

#include <devtools/ymake/diag/dbg.h>

#include <util/generic/fwd.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/output.h>
#include <util/stream/str.h>
#include <util/system/yassert.h>


// TODO: optimize/fix
static TStringBuf SkipSpaces(TStringBuf s) {
    while (s.at(0) == ' ' || s.at(0) == '\t')
        s.Skip(1);
    return s;
}
static TStringBuf SkipSpacesEx(const TSingleCmd::TCmdStr& s) {
    return SkipSpaces(std::get<TString>(s));
}

void TSubst2Shell::Start(const TStringBuf& cmd, TString&) {
    OrigCmd = cmd;
}

void TSubst2Shell::StartCommand(TString& /*res*/) {
    Commands.emplace_back();
}

void TSubst2Shell::FinishCommand(TString& res) {
    // TODO: really support env and cwd in multi-commands
    TSingleCmd& cmd = Commands.back();

    TString prepend;
    if (cmd.Cwd.size()) {
        prepend += TString::Join("cd ", cmd.Cwd, " && ");
        cmd.Cwd.clear();
    }

    for (const auto& env : cmd.EnvSetDefs) {
        prepend += env + " ";
    }
    cmd.EnvSetDefs.clear();

    if (prepend.size())
        res = prepend + res;

    if (cmd.StdOut.size()) {
        res += ">";
        res += cmd.StdOut;
        cmd.StdOut.clear();
    }

    cmd.CmdStr = std::move(res);
    res.clear();
}

void TSubst2Shell::Finish(TString& res, TCommandInfo&, const TVars&) {
    // TODO: fix messages
    Y_ASSERT(res.empty()); // all used in Finish()
    //CmdStr.swap(res);
    //YDIAG(MkCmd) << "CM-> " << OrigCmd << " -> " << CmdStr << "\n";
    if (Commands.empty() || Commands.size() == 1 && !SkipSpacesEx(Commands.back().CmdStr))
        throw TMakeError() << "Could not generate command for " << OrigCmd /*MainFileName*/;
}

ICommandSequenceWriter* TSubst2Shell::Upgrade() {
    return this;
}

void TSubst2Shell::BeginScript() {
}

void TSubst2Shell::BeginCommand() {
    Commands.emplace_back();
}

void TSubst2Shell::WriteArgument(TStringBuf arg) {
    TSingleCmd& cmd = Commands.back();
    TString& cmdStr = std::get<TString>(cmd.CmdStr);
    cmdStr += arg;
}

void TSubst2Shell::WriteCwd(TStringBuf cwd) {
    Commands.back().Cwd = cwd;
}

void TSubst2Shell::WriteStdout(TStringBuf path) {
    Commands.back().StdOut = path;
}

void TSubst2Shell::WriteEnv(TStringBuf env) {
    Commands.back().EnvSetDefs.emplace_back(env);
}

void TSubst2Shell::WriteResource(TStringBuf uri) {
    Commands.back().ResourceUris.emplace_back(uri);
}

void TSubst2Shell::WriteTaredOut(TStringBuf path) {
    Commands.back().TaredOuts.emplace_back(path);
}

void TSubst2Shell::EndCommand() {
    TSingleCmd& cmd = Commands.back();
    TString cmdStr = std::move(std::get<TString>(cmd.CmdStr));
    FinishCommand(cmdStr);
}

void TSubst2Shell::EndScript(TCommandInfo&, const TVars&) {
}

void TSubst2Shell::PostScript(TVars&) {
}

IOutputStream& TSubst2Shell::PrintAsLine(IOutputStream& out) const {
    bool lastWasSpace = false;
    for (const auto& cmd : Commands) {
        if (&cmd != Commands.data())
            out << (lastWasSpace ? "&&" : " &&");
        auto& cmdStr = std::get<TString>(cmd.CmdStr);
        out << cmdStr;
        lastWasSpace = cmdStr.size() && cmdStr.back() == ' ';
    }
    return out;
}

TString TSubst2Shell::PrintAsLine() const {
    TString str;
    TStringOutput so(str);
    PrintAsLine(so);
    return str;
}
