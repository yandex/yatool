#pragma once
#include <devtools/ymake/diag/dbg.h>
#include <util/generic/string.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>
#include <variant>

struct TMacroData;
struct TCommandInfo;
struct TVars;

//
// new-style sink for STRUCT_CMD
//

struct ICommandSequenceWriter {
    virtual void BeginScript() = 0;
    virtual void BeginCommand() = 0;
    virtual void WriteArgument(TStringBuf arg) = 0;
    virtual void WriteCwd(TStringBuf cwd) = 0;
    virtual void WriteStdout(TStringBuf path) = 0;
    virtual void WriteEnv(TStringBuf env) = 0;
    virtual void WriteResource(TStringBuf uri) = 0;
    virtual void WriteTaredOut(TStringBuf path) = 0;
    virtual void EndCommand() = 0;
    virtual void EndScript(TCommandInfo& cmdInfo, const TVars& vars) = 0;
    virtual void PostScript(TVars& vars) = 0;
protected:
    ~ICommandSequenceWriter() = default;
};

struct TCommandSequenceWriterStubs: ICommandSequenceWriter {
    virtual void WriteCwd(TStringBuf)          override { throw TNotImplemented() << "CWD is not supported here"; }
    virtual void WriteStdout(TStringBuf)       override { throw TNotImplemented() << "stdout settings are not supported here"; }
    virtual void WriteEnv(TStringBuf)          override { throw TNotImplemented() << "environment settings are not supported here"; }
    virtual void WriteResource(TStringBuf)     override { throw TNotImplemented() << "resource URIs are not supported here"; }
    virtual void WriteTaredOut(TStringBuf)     override { throw TNotImplemented() << "tared outputs are not supported here"; }
    virtual void PostScript(TVars&)            override {  }
protected:
    ~TCommandSequenceWriterStubs() = default;
};

//
// legacy serializer
//

struct TSingleCmd {
    using TCmdStr = std::variant<TString, TVector<TString>>; // either a JSON-ified array of args, or a straight up array of args
    TCmdStr CmdStr;
    TString Cwd;
    TString StdOut;
    TVector<TString> EnvSetDefs;
    TVector<TString> ResourceUris;
    TVector<TString> TaredOuts;
};

struct TMultiCmdDescr {
    // current state
    bool InToken = false; // we are still inside non-white-space-separated sequence of chars
    bool ContinueToken = true;

    TVector<TSingleCmd> Commands;
    // Callback handlers for SubstMacro
    virtual void Start(const TStringBuf& /*cmd*/, TString& /*res*/) {
    }
    virtual void ConvertText(TString& /*res*/, const TStringBuf& /*text*/) {
    } // only called with ECF_Json
    virtual void ConvertMacro(TString& /*res*/, TMacroData& /*macro*/) {
    } // only called with ECF_Json
    virtual void StartCommand(TString& res) = 0;
    virtual void FinishCommand(TString& res) = 0;
    virtual void Finish(TString& res, TCommandInfo& cmdInfo, const TVars& vars) = 0;

    virtual ICommandSequenceWriter* Upgrade() = 0;

protected:
    virtual ~TMultiCmdDescr() {
    }
};
