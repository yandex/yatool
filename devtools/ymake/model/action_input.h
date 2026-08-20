#pragma once

#include "../symbols/elem_id.h"

#include <util/generic/strbuf.h>

#include <optional>

// Producer observation needed to detect whether a previously resolved input
// resolves differently on a later configure. An empty ResolveDirectory
// represents resolution without an original directory; the model chooses its
// persisted encoding.
struct TInputResolutionRecord {
    TFileElemId OriginalPath;
    TFileElemId ResolveDirectory;
    TFileElemId ResultPath;
};

// Compatibility representation of an input after producer-side path
// resolution.  File is an opaque handle issued by the shared file table; the
// remaining fields describe the resolved artifact without graph vocabulary.
struct TResolvedActionInput {
    TFileElemId File;
    TStringBuf LogicalName;
    bool IsMacro;
    bool IsDirectory;
    bool IsOutput;
    bool MarkUsedAsInput;
    std::optional<TInputResolutionRecord> ResolutionRecord;
};

// Producer-facing semantic sink. Implementations may update model state, but
// path resolution does not receive or manipulate graph construction objects.
// InternLogicalPath is the restricted name-table operation needed to preserve
// the current allocation point for original input spellings.
class IActionInputModelSink {
public:
    virtual ~IActionInputModelSink() = default;

    virtual TFileElemId InternLogicalPath(TStringBuf path) = 0;
    virtual void AcceptResolvedInput(const TResolvedActionInput& input) = 0;
};
