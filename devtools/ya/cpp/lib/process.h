#pragma once

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/hash.h>
#include <util/folder/path.h>

namespace NYa {
    using TExecve = std::function<void(TFsPath bin, TVector<TString> args, const THashMap<TString, TString>& env, const TFsPath& cwd)>;
    void Execve(TFsPath bin, TVector<TString> args, const THashMap<TString, TString>& env, const TFsPath& cwd);
}
