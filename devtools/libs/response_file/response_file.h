#pragma once

#include <util/generic/array_ref.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <cstddef>
#include <functional>

namespace NResponseFile {

    inline constexpr size_t MaxExpandedArguments = 100000;
    inline constexpr size_t MaxResponseFileNesting = 64;

    // args contains non-null pointers, including argv[0], which is passed through unchanged.
    // Ordinary arguments borrow pointers from args; @@arg borrows args[i] + 1.
    // Arguments read from files are appended to storage. Keep both the original
    // arguments and storage alive and unmodified while using the result. Input
    // pointers must not refer to storage, which may reallocate during expansion.
    // The result has no trailing nullptr. Errors throw yexception and may leave
    // partially appended strings in storage.
    // stopExpansion sees each argument before expansion/unescaping, except argv[0].
    // Once it returns true, that argument and all following arguments are literal,
    // including remaining lines of already open files and arguments in their callers.
    TVector<const char*> ExpandResponseFiles(
        TConstArrayRef<const char*> args,
        TVector<TString>& storage,
        const std::function<bool(TStringBuf)>& stopExpansion = {});

} // namespace NResponseFile
