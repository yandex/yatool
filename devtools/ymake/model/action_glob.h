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

// Producer result of evaluating one glob input. Logical paths cross the
// boundary by value; the model assigns file-table handles and chooses their
// persisted link encoding and all graph/cache topology.
struct TEvaluatedActionGlob {
    TString Pattern;
    TString MatchesHash;
    TVector<TString> WatchedDirectories;
    TVector<TString> MatchedPaths;
};
