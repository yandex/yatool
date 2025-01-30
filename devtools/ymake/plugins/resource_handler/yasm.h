#pragma once
#include "packer.h"

namespace NYMake::NResourcePacker {

    /* This Packer use assembler codegeneration for resource insertion */
    class TYASMResourcePacker: public IResourcePacker {
    public:
        TYASMResourcePacker(TPluginUnit& Unit, bool UseTextContext)
            : IResourcePacker(Unit)
            , UseTextContext_{UseTextContext}
        {
        }
        void HandleResource(TStringBuf path, TStringBuf name) override {
            TString lid = TString::Join("ro_", GetHashForOutput(TVector<TStringBuf>({path, name})));
            TString output = TString::Join(lid, ".rodata");

            if (path == "-") {
                TStringBuf splitedPath, splitedName;
                name.Split('=', splitedName, splitedPath);
                if (splitedPath.empty() || splitedName.empty()) {
                    YConfErr(Syntax) << "Syntax error: empty KEY or VALUE in RESOURCE(- KEY=VALUE), guard with \"\"" << Endl;
                }
                append(Compressed_, "-"sv, splitedPath, output);
                append(SrcGen_, TString::Join(lid, "=", splitedName));
            } else {
                append(Compressed_, path, output);
                append(CompressedInputs_, path);
                append(CompressedOutputs_, output);
                append(SrcGen_, TString::Join(lid, "=", name));
            }
            EstimatedCmdLen_ += ROOT_CMD_LEN + path.length() + name.length();
            Finalize(false /*force*/);
        }
        void Finalize(bool force) override {
            if ((EstimatedCmdLen_ < MAX_CMD_LEN) && !force) {
                return;
            }
            EmitYASM();
            EmitExtCPP();
            if (force) {
                JoinSources();
            }
        }

    private:
        void EmitYASM() {
            if (Compressed_.empty()) {
                return;
            }

            TString lid = GetHashForOutput(TVector<TStringBuf>(Compressed_.begin(), Compressed_.end()));
            TString fakeYasm = TString::Join(lid, ".yasm");

            bool needPrefix = Unit_.Enabled("DARWIN"sv) || Unit_.Enabled("IOS"sv) || Unit_.Enabled("WINDOWS"sv) && Unit_.Enabled("ARCH_TYPE_32"sv);
            TVector<TString> cmdArgs;
            append(cmdArgs, Unit_.Get("_TOOL_RESCOMPRESSOR"sv), fakeYasm);

            append_if(needPrefix, cmdArgs, "--prefix"sv);
            append(cmdArgs, "$YASM_DEBUG_INFO_DISABLE_CACHE__NO_UID__"sv, Compressed_);
            append_if(!CompressedInputs_.empty(), cmdArgs, "IN"sv, CompressedInputs_);

            if (UseTextContext_) {
                auto inputsMap = PrepareInputs(CompressedInputs_);
                ReplaceInputs(cmdArgs, inputsMap);
            }

            append(cmdArgs, "OUT_NOAUTO"sv, fakeYasm, CompressedOutputs_);
            RunMacro("RUN_PROGRAM"sv, cmdArgs);

            Compressed_.clear();
            CompressedInputs_.clear();
            CompressedOutputs_.clear();

            YasmOutput_.push_back(fakeYasm);
        }

        void EmitExtCPP() {
            if (SrcGen_.empty()) {
                return;
            }
            TString lid = GetHashForOutput(TVector<TStringBuf>(SrcGen_.begin(), SrcGen_.end()));
            TString output = TString::Join(lid, AUX_CPP_EXT);
            TVector<TString> cmdArgs;
            append(cmdArgs, Unit_.Get("_TOOL_RORESCOMPILER"sv), output, SrcGen_, "OUT_NOAUTO"sv, output);
            RunMacro("RUN_PROGRAM"sv, cmdArgs);
            SrcGen_.clear();

            CppOutput_.push_back(output);
        }

        void JoinSources() {
            auto getUniqJoinName = [this](const auto& iterable, TStringBuf ext) -> TString {
                auto inputs = TVector<TStringBuf>(iterable.begin(), iterable.end());
                auto hash = GetHashForOutput(std::move(inputs));
                return TString::Join("join_", hash, ext);
            };
            if (CppOutput_) {
                if (CppOutput_.size() > 1) {
                    auto joinedOutput = getUniqJoinName(CppOutput_, AUX_CPP_EXT);
                    append(CppOutput_, joinedOutput);
                    Unit_.CallMacro("JOIN_SRCS_GLOBAL", TVector<TStringBuf>(CppOutput_.rbegin(), CppOutput_.rend()));
                } else {
                    CppOutput_.push_back("GLOBAL");
                    Unit_.CallMacro("SRCS", TVector<TStringBuf>(CppOutput_.rbegin(), CppOutput_.rend()));
                }
            }
            if (YasmOutput_) {
                if (YasmOutput_.size() > 1) {
                    auto joinedOutput = getUniqJoinName(YasmOutput_, ".yasm");
                    append(YasmOutput_, joinedOutput);
                    Unit_.CallMacro("FLAT_JOIN_SRCS_GLOBAL", TVector<TStringBuf>(YasmOutput_.rbegin(), YasmOutput_.rend()));
                } else {
                    Unit_.CallMacro("SRCS", {*YasmOutput_.begin()});
                }
            }
        }

    private:
        bool UseTextContext_{};
        int EstimatedCmdLen_ = 0;
        TVector<TString> Compressed_{};
        TVector<TString> CompressedInputs_{};
        TVector<TString> CompressedOutputs_{};
        TVector<TString> SrcGen_{};
        TVector<TString> CppOutput_{};
        TVector<TString> YasmOutput_{};
    };
} // namespace NYMake::NResourcePacker
