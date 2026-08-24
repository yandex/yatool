#pragma once

#include <util/generic/list.h>
#include <util/generic/maybe.h>
#include <util/generic/string.h>
#include <util/system/shellcommand.h>

namespace NYa::NSnowden::NPrivate {
    TMaybe<int> RunPythonEntryPoint(
        const TString& executable,
        const TString& entryPoint,
        const TList<TString>& args,
        bool async
    );
}
