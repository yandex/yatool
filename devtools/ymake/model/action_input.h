#pragma once

#include "../symbols/elem_id.h"

#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <optional>

// Producer observation needed to detect whether a previously resolved input
// resolves differently on a later configure. An empty ResolveDirectory
// represents resolution without an original directory; the model interns the
// logical paths and chooses their persisted encoding at commit time.
struct TInputResolutionRecord {
    TString OriginalPath;
    TString ResolveDirectory;
    TString ResultPath;
};

// Compatibility representation of an input after producer-side path
// resolution. LogicalName is normalized but deliberately not interned in this
// value, so producer/model ordering does not leak through element IDs.
struct TResolvedActionInput {
    TString LogicalName;
    bool IsMacro;
    bool IsDirectory;
    bool IsOutput;
    bool MarkUsedAsInput;
    std::optional<TInputResolutionRecord> ResolutionRecord;
};

using TResolvedActionInputs = TVector<TResolvedActionInput>;
