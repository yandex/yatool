#include "plugin_go_fake_output_handler.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/conf.h>
#include <devtools/ymake/lang/plugin_facade.h>
#include <devtools/ymake/macro_processor.h>
#include <devtools/ymake/yndex/yndex.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/generic/strbuf.h>
#include <util/generic/string.h>

namespace {
    const TStringBuf TouchCmdPrefix = "${YMAKE_PYTHON3} ${ARCADIA_ROOT}/build/scripts/touch.py ";
    const TString FakeMacroName = "GO_FAKE_OUTPUT";

    class TFakeOutputCmd: public TMacroCmd {
    public:
        TFakeOutputCmd() {
            AddToInput(TStringBuf("build/scripts/touch.py"));
        }

        ~TFakeOutputCmd() override = default;

        void Output(TSpecFileList& res) const override {
            CmdOutput.CopyTo(res);
        }

        void OutputInclude(TSpecFileList& res) const override {
            CmdOutputInclude.CopyTo(res);
        }

        void Input(TSpecFileList& res) const override {
            CmdInput.CopyTo(res);
        }

        void Tools(TVector<TString>& res) const override {
            res = CmdTools;
        }

        TString ToString() const override {
            return CmdStr;
        }

        void AddToOutput(TStringBuf outout) {
            TVarStrEx varStr(outout);
            varStr.NoAutoSrc = true;
            CmdOutput.Push(varStr);
        }

        void AddToInput(TStringBuf input) {
            const TVarStrEx varStr(input);
            CmdInput.Push(varStr);
        }

        void SetCmd(TStringBuf cmdStr) {
            CmdStr = TString{cmdStr};
        }

    private:
        TSpecFileList CmdOutput;
        TSpecFileList CmdOutputInclude;
        TSpecFileList CmdInput;
        TVector<TString> CmdTools;
        TString CmdStr;
        TString OutDir;
    }; // end of TFakeCmd class

    void ExecuteGoFakeOutput(TPluginUnit& unit, const TVector<TStringBuf>& params, TVector<TSimpleSharedPtr<TMacroCmd>>* result) {
        auto cmd = new TFakeOutputCmd();
        TSimpleSharedPtr<TMacroCmd> cmdPtr(cmd);
        MD5 md5;
        auto isEmptyFilesList = true;

        for (auto param : params) {
            if (!param.EndsWith(TStringBuf(".go"))) {
                // Nothing to do
                continue;
            }

            const TString resolved = unit.ResolveToArcPath(param);
            if (!NPath::IsTypedPath(resolved) || !NPath::IsType(resolved, NPath::ERoot::Source)) {
                // We will report this failure later trying to resolve inputs for link command of go package
                continue;
            }

            TStringBuf fileName = NPath::CutType(resolved);
            cmd->AddToInput(fileName);
            md5.Update(param);
            isEmptyFilesList = false;
        }

        if (!isEmptyFilesList) {
            char tempBuf[33];
            TString output = TString::Join(TStringBuf("${BINDIR}/."), TStringBuf{md5.End(tempBuf), 32});
            TString cmdStr = TString::Join(TouchCmdPrefix, output);
            cmd->AddToOutput(output);
            cmd->SetCmd(cmdStr);
            result->push_back(cmdPtr);
        }
    } // end of ExecuteScanGoImports
} // end of anonymous namespace

#define __FAKE_MACRO_NAME__ "GO_FAKE_OUTPUT"

namespace NYMake {
    namespace NPlugins {
        static const TSourceLocation docLink = __LOCATION__;
        void TPluginGoFakeOutputHandler::Execute(TPluginUnit& unit, const TVector<TStringBuf>& params, TVector<TSimpleSharedPtr<TMacroCmd>>* result) {
            ExecuteGoFakeOutput(unit, params, result);
        }

        void TPluginGoFakeOutputHandler::RegisterMacro(TBuildConfiguration& conf) {
            TString docText = TString::Join("@usage: ", FakeMacroName, "(go-src-files...)");
            auto macro = MakeSimpleShared<TPluginGoFakeOutputHandler>();
            macro->Definition = {
                std::move(docText),
                TString{docLink.File},
                static_cast<size_t>(docLink.Line) + 1,
                0,
                static_cast<size_t>(docLink.Line) + 1,
                0
            };
            conf.RegisterPluginMacro(FakeMacroName, macro);
        }
    } // end of namespace NPlugins
} // end of namespace NYMake

