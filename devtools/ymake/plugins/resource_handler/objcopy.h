#pragma once
#include "packer.h"

#include <util/folder/path.h>
#include <util/string/hex.h>

namespace NYMake::NResourcePacker {

    /* This Packer use llvm-objcopy for resource insertion */
    class TObjCopyResourcePacker: public IResourcePacker {
    public:
        TObjCopyResourcePacker(TPluginUnit& Unit, bool UseTextContext)
            : IResourcePacker(Unit)
            , UseTextContext_{UseTextContext}
        {
        }
        void HandleResource(TStringBuf path, TStringBuf name) override {
            if (path != "-") {
                // Handle PATH KEY
                Objects_.paths.emplace_back(path);
                Objects_.keys.emplace_back(Base64Encode(name));
                EstimatedCmdLen_ += ROOT_CMD_LEN + path.length() + Objects_.keys.back().length();
            } else {
                // Handle - KEY=VALUE
                Objects_.kvs.emplace_back(name);
                EstimatedCmdLen_ += ROOT_CMD_LEN + name.length();
            }
            Finalize(false /*force*/);
        }
        void Finalize(bool force) override {
            if (EstimatedCmdLen_ == 0) {
                return;
            }
            if ((EstimatedCmdLen_ < MAX_CMD_LEN) && !force) {
                return;
            }
            TVector<TStringBuf> kv;
            append(kv, Objects_.paths, Objects_.keys, Objects_.kvs);
            TString lid = GetHashForOutput(std::move(kv));
            TString outputObj = TString::Join("${BINDIR}/objcopy_", lid, ".o");

            auto targetPlatform = GetTargetPlatform();
            TVector<TString> cmdArgs;
            append(cmdArgs,
                   "build/scripts/objcopy.py"sv,
                   "--compiler"sv, Unit_.Get("CXX_COMPILER"),
                   "--objcopy"sv, Unit_.Get("OBJCOPY_TOOL"),
                   "--compressor"sv, Unit_.Get("_TOOL_RESCOMPRESSOR"),
                   "--rescompiler"sv, Unit_.Get("_TOOL_RESCOMPILER"),
                   "--output_obj"sv, "${output:__OUT}"sv,
                   "--target"sv, targetPlatform);
            append_if(Objects_.paths.size() > 0, cmdArgs, "--inputs"sv, "${input:__PATHS}"sv, "--keys"sv, "$__KEYS"sv);
            append_if(Objects_.kvs.size() > 0, cmdArgs, "--kvs"sv, Objects_.kvs);
            append(cmdArgs,
                   "TOOL"sv, Unit_.Get("_TOOL_RESCOMPRESSOR"), Unit_.Get("_TOOL_RESCOMPILER"),
                   "OUT_GLOBAL"sv, "$__OUT"sv);

            if (UseTextContext_) {
                auto inputsMap = PrepareInputs(Objects_.paths);
                ReplaceInputs(Objects_.paths, inputsMap);
            }

            TVars extras;
            // TODO ~~ring them bells~~ move them strings
            extras["__OUT"].push_back(outputObj);
            extras["__OUT"].NoInline = true;
            extras["__PATHS"].Assign(Objects_.paths);
            extras["__PATHS"].NoInline = true;
            extras["__KEYS"].Assign(Objects_.keys);
            extras["__KEYS"].NoInline = true;
            extras["__KEYS"].DontParse = true;

            RunMacro("RUN_PYTHON3"sv, cmdArgs, extras);

            EstimatedCmdLen_ = 0;
            Objects_.paths.clear();
            Objects_.keys.clear();
            Objects_.kvs.clear();
        }

        static bool CanHandle(TStringBuf path, TStringBuf name) {
            constexpr std::string_view BAD[] = {
                "${ARCADIA_BUILD_ROOT}",
                "${ARCADIA_SOURCE_ROOT}",
                "conftest.py",
            };
            bool ok = true;
            for (auto view : BAD) {
                ok = ok && name.find(view) == std::string::npos;
                ok = ok && path.find(view) == std::string::npos;
            }
            return ok;
        }

    private:
        struct {
            TVector<TString> paths;
            TVector<TString> keys;
            TVector<TString> kvs;
        } Objects_{};
        bool UseTextContext_{};
        int EstimatedCmdLen_{};

    private:
        TString GetTargetPlatform() const {
            auto flags = Unit_.Get("C_FLAGS_PLATFORM");
            constexpr TStringBuf CXX_TARGET_FLAG = "--target="sv;
            TString target;
            if (auto idx = flags.find(CXX_TARGET_FLAG); idx != std::string::npos) {
                idx += CXX_TARGET_FLAG.size();
                auto end = idx;
                while (end < flags.size() && !std::isspace(flags[end])) {
                    end++;
                }
                target = flags.substr(idx, end - idx);
            }
            if (target.empty()) {
                target = "__unknown_target__";
            }
            return target;
        }
    };

} // namespace NYMake::NResourcePacker
