#pragma once

#include <util/generic/list.h>
#include <util/generic/maybe.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/shellcommand.h>

namespace NYa::NSnowden::NPrivate {
    TShellCommandOptions BuildPythonEntryPointOptions(bool async);

    TMaybe<int> RunPythonEntryPoint(
        const TString& executable,
        const TString& entryPoint,
        const TList<TString>& args,
        bool async
    );

    TList<TString> BuildToolHandlerEventArguments(
        const TVector<TString>& expandedArgs,
        const TVector<TString>& toolNameParts,
        const TVector<TString>& toolArgs
    );

    TList<TString> BuildToolExecutionEventArguments(
        const TString& toolName,
        const TString& toolPath,
        const TVector<TString>& toolArgs
    );
}
