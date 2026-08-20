#include "action_glob_evaluator.h"

#include "../glob_helper.h"

#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/symbols/file_store.h>

#include <library/cpp/regex/pcre/regexp.h>

TActionGlobEvaluator::TActionGlobEvaluator(TActionGlobEvaluationContext context)
    : Context_(context)
{
}

TEvaluatedActionGlob TActionGlobEvaluator::Evaluate(TStringBuf pattern) {
    auto& fileConf = *Context_.FileConf_;
    TExcludeMatcher excludeMatcher;
    TUniqVector<TFileElemId> matches;
    TGlobPattern glob(fileConf, pattern, fileConf.GetName(Context_.RootDirectory_));
    for (const auto& result : glob.Apply(excludeMatcher)) {
        matches.Push(result.GetTargetId());
    }
    return TEvaluatedActionGlob{
        .Pattern = TString{pattern},
        .MatchesHash = glob.GetMatchesHash(),
        .WatchedDirectories = glob.GetWatchDirs().Data(),
        .MatchedPaths = matches.Take(),
    };
}

void TActionGlobEvaluator::ReportInvalidPattern(TStringBuf pattern, TStringBuf error) {
    YConfErr(Syntax) << "Invalid pattern in [[alt1]]" << pattern << "[[rst]]: " << error << Endl;
}
