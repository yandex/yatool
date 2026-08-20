#include "action_path_resolver.h"

#include "../macro_processor.h"
#include "../macro_string.h"
#include "../module_builder.h"

#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/diag.h>

#include <util/generic/algorithm.h>

namespace {
    std::optional<TInputResolutionRecord> MakeInputResolutionRecord(
        const TVarStrEx& input,
        TStringBuf originalInput,
        TFileView currentDirectory,
        TModuleBuilder& moduleBuilder,
        IActionInputModelSink& modelSink
    ) {
        if (originalInput == input.Name) {
            return std::nullopt;
        }
        if (NPath::IsTypedPathEx(originalInput)) {
            switch (NPath::GetType(originalInput)) {
            case NPath::Link:
                if (!NPath::IsType(NPath::GetTargetFromLink(originalInput), NPath::Unset)) {
                    return std::nullopt;
                }
                [[fallthrough]];
            case NPath::Unset:
                break;
            case NPath::Source:
            case NPath::Build:
                return std::nullopt;
            }
        } else {
            TString knownPath;
            if (moduleBuilder.Resolver().ResolveAsKnownWithoutCheck(originalInput, currentDirectory, knownPath)) {
                return std::nullopt;
            }
        }

        return TInputResolutionRecord{
            .OriginalPath = modelSink.InternLogicalPath(
                NPath::IsTypedPathEx(originalInput)
                    ? originalInput
                    : NPath::ConstructPath(originalInput, NPath::Unset)
            ),
            .ResolveDirectory = currentDirectory.IsValid() ? currentDirectory.GetElemId() : TFileElemId(),
            .ResultPath = AssumeFile(input.ElemId),
        };
    }

    TVarStrEx* FindMainInputOrDefault(std::span<TVarStrEx> inputs, ui32 defaultPosition) {
        if (inputs.empty()) {
            return nullptr;
        }

        auto isMain = [](const auto& input) { return input.Main; };
        auto it = FindIf(inputs, isMain);
        if (it != inputs.end()) {
            TVarStrEx* mainInput = &*it;
            if (std::any_of(++it, inputs.end(), isMain)) {
                YConfWarn(NoMain) << "Two or more main elements; picked the first one" << Endl;
            }
            return mainInput;
        }

        if (defaultPosition != Max<ui32>()) {
            if (inputs.size() > 1) {
                YConfWarn(NoMain) << "No explicit main element; picked the default one" << Endl;
            }
            return &inputs[defaultPosition];
        }
        return nullptr;
    }

    bool CheckForDirectory(const TVarStrEx& input, const TYVar& command, TStringBuf description) {
        if (!Y_UNLIKELY(input.IsDir)) {
            return true;
        }

        TStringBuf commandName;
        auto tryParse = [&](const TYVar& value) {
            if (value.size() != 1 || value[0].StructCmdForVars) {
                return false;
            }
            TStringBuf commandValue;
            ui64 id;
            ParseCommandLikeVariable(Get1(&value), id, commandName, commandValue);
            return true;
        };
        if (!tryParse(command) && (!command.BaseVal || !tryParse(*command.BaseVal))) {
            commandName = "[unspecified macro]";
        }

        YConfErr(BadInput) << description << " in " << commandName << " " << input.Name
                           << " points to a directory and cannot be processed. Please provide a file path instead."
                           << Endl;
        return false;
    }
}

EActionInputResolution TActionInputResolver::Resolve(
    TCommandInfo& commandInfo,
    TModuleBuilder& moduleBuilder,
    IActionInputModelSink& modelSink,
    bool lastTry
) const {
    commandInfo.Finalize();

    auto inputs = commandInfo.GetInput();
    commandInfo.MainInput = FindMainInputOrDefault(inputs, commandInfo.MainInputCandidateIdx);
    if (commandInfo.MainInput && !commandInfo.InitDirs(*commandInfo.MainInput, moduleBuilder, lastTry)) {
        YDIAG(VV) << "Main input in " << Get1(&commandInfo.Cmd)
                  << " is not ready, delay processing" << Endl;
        return EActionInputResolution::Pending;
    }

    for (auto& input : inputs) {
        if (input.IsGlob) {
            continue;
        }

        const TString originalInput = input.Name;
        if (!moduleBuilder.ResolveSourcePath(
                input,
                commandInfo.InputDir,
                lastTry ? TModuleBuilder::LastTry : TModuleBuilder::Default
            ) && !lastTry) {
            YDIAG(VV) << "Input '" << input.Name << "' in " << Get1(&commandInfo.Cmd)
                      << " is not ready, delay processing" << Endl;
            return EActionInputResolution::Pending;
        }

        if (!input.DirAllowed && !CheckForDirectory(input, commandInfo.Cmd, "input dependency"sv)) {
            return EActionInputResolution::Skipped;
        }

        Y_ASSERT(input.ElemId); // must exists if ResolveSourcePath is true
        modelSink.AcceptResolvedInput({
            .File = AssumeFile(input.ElemId),
            .LogicalName = input.Name,
            .IsMacro = input.IsMacro,
            .IsDirectory = input.IsDir,
            .IsOutput = input.IsOutputFile,
            .MarkUsedAsInput = !input.ByExtFailed,
            .ResolutionRecord = MakeInputResolutionRecord(
                input,
                originalInput,
                commandInfo.InputDir,
                moduleBuilder,
                modelSink
            ),
        });
    }

    EActionInputResolution state = EActionInputResolution::Ready;
    commandInfo.ApplyToOutputIncludes([&](TStringBuf type, TSpecFileArr& outputIncludes) {
        Y_UNUSED(type);

        for (auto& outputInclude : outputIncludes) {
            if (outputInclude.OutInclsFromInput) {
                // Try to resolve as input immediately
                moduleBuilder.ResolveSourcePath(outputInclude, commandInfo.InputDir, TModuleResolver::LastTry);
                if (!CheckForDirectory(outputInclude, commandInfo.Cmd, "output include"sv)) {
                    state = EActionInputResolution::Skipped;
                    return;
                }
            } else {
                // Only try to resolve as known by default (without FS check),
                // delay actual resolving as include until InducedDeps property is applied.
                moduleBuilder.ResolveAsKnownWithoutCheck(outputInclude);
                auto resolved = moduleBuilder.MakeUnresolved(outputInclude.Name);
                outputInclude.Name = moduleBuilder.GetStr(resolved);
                outputInclude.ElemId = resolved.GetElemId();
            }
            Y_ASSERT(outputInclude.ElemId); // Must be filled in all ways
        }
    });

    return state;
}

bool TActionOutputResolver::Resolve(
    TCommandInfo& commandInfo,
    TModuleBuilder& moduleBuilder
) const {
    const TStringBuf commandName = Get1(&commandInfo.Cmd);
    YDIAG(Dev) << "Process command: " << commandName << Endl;
    for (auto& output : commandInfo.GetOutput()) {
        if (!moduleBuilder.FormatBuildPath(output, commandInfo.InputDir, commandInfo.BuildDir)) {
            YConfErr(BadOutput) << "Directory " << output.Name << " is not allowed as output. Skip command: "
                                << SkipId(commandName) << Endl;
            return false;
        }
        YDIAG(Dev) << "Process: AddName " << output.Name << Endl;
    }
    return true;
}
