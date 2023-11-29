#pragma once

#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/symbols/name_store.h>

#include <util/generic/string.h>
#include <util/generic/strbuf.h>
#include <util/string/vector.h>
#include <util/string/builder.h>

class TFileConf;
class TRegExMatch;

class TExcludeMatcher {
public:
    void AddExcludePattern(TFileView moddir, TStringBuf pattern);

    bool IsExcluded(TFileView path) const;

private:
    TVector<TRegExMatch> Matchers;
};

struct TGlobPart {
    enum class EGlobType {
        Fixed,    // "$S/devtools"
        Pattern,  // "*make"
        Anything, // "*"
        Recursive // "**"
    };

    EGlobType Type;
    TString Data;
};

// Short-live object with 2 scenarios of usage:
// 1. TGlob (pattern) -> Apply -> dump to property (GetWatchDirs + GetMatchesHash)
// 2. WatchDirsUpdated -> TGlob (property) -> NeedUpdate
class TGlob {
private:
    TFileConf& FileConf;

    TString Pattern;
    TFileView RootDir;
    TVector<TGlobPart> Parts;

    TUniqVector<ui32> WatchDirs;
    TString MatchesHash;

public:
    TGlob(TFileConf& fileConf, TStringBuf glob, TFileView rootDir);

    TGlob(TFileConf& fileConf, TFileView rootDir, TStringBuf pattern, TStringBuf hash, TUniqVector<ui32>&& oldWatchDirs);

    // Watch-dir timestamp changed -> apply glob
    static bool WatchDirsUpdated(TFileConf& fileConf, const TUniqVector<ui32>& watchDirs);

    // Is hash(matches) or WatchDirs list changed
    bool NeedUpdate(const TExcludeMatcher& excludeMatcher);

    // Returns list of files, matched by the glob pattern
    TVector<TFileView> Apply(const TExcludeMatcher& excludeMatcher);

    const TUniqVector<ui32>& GetWatchDirs() const {
        return WatchDirs;
    }

    const TString& GetMatchesHash() const noexcept {
        return MatchesHash;
    }

private:
    void ParseGlobPattern();

    bool ApplyFixedPart(TVector<TFileView>& newDirs, TVector<TFileView>& matches, ui32 id, const bool isLastPart, const TExcludeMatcher& excludeMatcher) const;
    void ApplyRecursivePart(TVector<TFileView>& newDirs, ui32 dirId) const;
    void ApplyPatternPart(TVector<TFileView>& newDirs, TVector<TFileView>& matches, const std::function<bool(TStringBuf)>& matcher, ui32 dirId, const bool isLastPart, const TExcludeMatcher& excludeMatcher) const;
};

// Transforms ANT-like glob pattern to a regular expression. Usable in case of matching set of paths against
// some pattern without searching paths matching it on filesystem or in filetables.
TString PatternToRegexp(TStringBuf pattern);
bool MatchPath(const TRegExMatch& globPattern, TFileView graphPath);
