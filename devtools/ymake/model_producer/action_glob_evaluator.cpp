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

std::optional<TEvaluatedActionGlob> TActionGlobEvaluator::Evaluate(TStringBuf pattern) const {
    try {
        auto& fileConf = *Context_.FileConf_;
        TExcludeMatcher excludeMatcher;
        TUniqVector<TString> matches;
        TGlobPattern glob(fileConf, pattern, fileConf.GetName(Context_.RootDirectory_));
        for (const auto& result : glob.Apply(excludeMatcher)) {
            matches.Push(TString{result.GetTargetStr()});
        }
        TVector<TString> watchedDirectories;
        watchedDirectories.reserve(glob.GetWatchDirs().size());
        for (const auto directory : glob.GetWatchDirs()) {
            watchedDirectories.push_back(TString{fileConf.GetName(directory).GetTargetStr()});
        }
        return TEvaluatedActionGlob{
            .Pattern = TString{pattern},
            .MatchesHash = glob.GetMatchesHash(),
            .WatchedDirectories = std::move(watchedDirectories),
            .MatchedPaths = matches.Take(),
        };
    } catch (const yexception& error) {
        YConfErr(Syntax) << "Invalid pattern in [[alt1]]" << pattern << "[[rst]]: " << error.what() << Endl;
        return std::nullopt;
    }
}
