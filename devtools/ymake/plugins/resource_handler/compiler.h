#pragma once
#include "packer.h"

namespace NYMake::NResourcePacker {

    /* This Packer use cpp codegeneration for resource insertion */
    class TRawResourcePacker: public IResourcePacker {
    public:
        TRawResourcePacker(TPluginUnit& Unit, bool UseTextContext)
            : IResourcePacker(Unit)
            , UseTextContext_{UseTextContext}
        {
        }
        void HandleResource(TStringBuf path, TStringBuf name) override {
            if (path != "-") {
                // /path/to/file KEY
                append(Gens_, path, TString::Join("-", name));
                append(Inputs_, path);
            } else {
                // - KEY=VALUE
                append(Gens_, path, name);
            }
            EstimatedCmdLen_ += ROOT_CMD_LEN + path.length() + name.length();
            Finalize(false /*force*/);
        }
        void Finalize(bool force) override {
            if (Inputs_.empty() && Gens_.empty()) {
                return;
            }
            if ((EstimatedCmdLen_ < MAX_CMD_LEN) && !force) {
                return;
            }
            TString lid = GetHashForOutput(TVector<TStringBuf>(Gens_.begin(), Gens_.end()));
            TString output = TString::Join(lid, "_raw", AUX_CPP_EXT);

            TVector<TString> cmdArgs;
            append(cmdArgs, Unit_.Get("_TOOL_RESCOMPILER"sv), output, Gens_);
            append_if(!Inputs_.empty(), cmdArgs, "IN"sv, Inputs_);

            if (UseTextContext_) {
                auto inputsMap = PrepareInputs(Inputs_);
                ReplaceInputs(cmdArgs, inputsMap);
            }
            append(cmdArgs, "OUT_NOAUTO"sv, output);
            RunMacro("RUN_PROGRAM"sv, cmdArgs);
            Unit_.CallMacro("SRCS"sv, {"GLOBAL"sv, output});

            Inputs_.clear();
            Gens_.clear();
            EstimatedCmdLen_ = 0;
        }

    private:
        TVector<TString> Inputs_{};
        TVector<TString> Gens_{};
        int EstimatedCmdLen_{};
        bool UseTextContext_{};
    };

} // namespace NYMake::NResourcePacker
