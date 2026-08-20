#pragma once

#include "../symbols/elem_id.h"

#include <util/generic/string.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>

class TActionGlobEvaluator;
class TActionGraphEncoder;
class TFileConf;

// Restricted model-owned capability used by the compatibility evaluator. It
// deliberately exposes no graph or file-table operations in the public API.
class TActionGlobEvaluationContext {
private:
    friend class TActionGlobEvaluator;
    friend class TActionGraphEncoder;

    TActionGlobEvaluationContext(TFileConf& fileConf, TFileElemId rootDirectory)
        : FileConf_(&fileConf)
        , RootDirectory_(rootDirectory)
    {
    }

    TFileConf* FileConf_;
    TFileElemId RootDirectory_;
};

// Producer result of evaluating one glob input. File-table handles are the
// restricted name interface shared with the producer; the model still chooses
// their persisted link encoding and all graph/cache topology.
struct TEvaluatedActionGlob {
    TString Pattern;
    TString MatchesHash;
    TVector<TFileElemId> WatchedDirectories;
    TVector<TFileElemId> MatchedPaths;
};

// Compatibility seam which keeps evaluation at its historical point in
// action encoding. A future atomic submission can carry the evaluated values
// directly instead of exposing a lazy producer callback.
class IActionGlobEvaluator {
public:
    virtual ~IActionGlobEvaluator() = default;

    virtual TEvaluatedActionGlob Evaluate(TStringBuf pattern) = 0;
    virtual void ReportInvalidPattern(TStringBuf pattern, TStringBuf error) = 0;
};
