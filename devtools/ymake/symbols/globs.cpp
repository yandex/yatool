#include "globs.h"

#include "symbols.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/manager.h>

#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/regex/pcre/regexp.h>

#include <util/generic/queue.h>
#include <util/generic/yexception.h>
#include <util/string/builder.h>

namespace {
    const auto GLOB_ANY_CHAR = '?';
    const auto GLOB_ANY_STR = '*';
    const auto GLOB_ANY_RECURSIVE = "**";

    const auto REGEX_ANY_CHAR = ".";
    const auto REGEX_ANY_STR = ".*";
    const auto REGEX_ANY_FILENAME_CHAR = "[^/]";
    const auto REGEX_ANY_FILENAME_STR = "[^/]*";
    const auto REGEX_START = '^';
    const auto REGEX_END = '$';
    const auto REGEX_ESCAPE = '\\';

    void GlobSegmentToRegex(TStringBuilder& dest, TStringBuf& segment, bool singleSegment) {
        for (auto ch : segment) {
            if (ch == GLOB_ANY_STR) {
                dest << (singleSegment ? REGEX_ANY_STR : REGEX_ANY_FILENAME_STR);
            } else if (ch == GLOB_ANY_CHAR) {
                dest << (singleSegment ? REGEX_ANY_CHAR : REGEX_ANY_FILENAME_CHAR);
            } else if (ch == '.') {
                dest << REGEX_ESCAPE << ch;
            } else {
                dest << ch;
            }
        }
    }

    TString GlobToRegex(TStringBuf pattern) {
        TStringBuilder res;
        res << REGEX_START;
        GlobSegmentToRegex(res, pattern, true);
        res << REGEX_END;
        return res;
    }

    TStringBuf TryCutCurdir(TStringBuf glob) noexcept {
        constexpr auto CURDIR = "$CURDIR/"sv;
        constexpr auto BRACED_CURDIR = "${CURDIR}/"sv;

        if (glob.starts_with(CURDIR)) {
            return glob.substr(CURDIR.size());
        }
        if (glob.starts_with(BRACED_CURDIR)) {
            return glob.substr(BRACED_CURDIR.size());
        }
        return glob;
    }
}

TGlob::TGlob(TFileConf& fileConf, TStringBuf glob, TFileView rootDir)
    : FileConf(fileConf)
    , RootDir(rootDir)
{
    if (!NPath::ToYPath(glob, Pattern)) {
        RootDir = rootDir;
        Pattern = TryCutCurdir(glob);
    } else {
        auto type = NPath::GetType(Pattern);
        if (type != NPath::Source) {
            throw yexception() << "this type of root is forbidden";
        }
        Y_ASSERT(type == NPath::Source);
        RootDir = FileConf.SrcDir();
        Pattern = NPath::CutType(Pattern);
    }

    ParseGlobPattern();
}

TGlob::TGlob(TFileConf& fileConf, TFileView rootDir, TStringBuf pattern, TStringBuf hash, TUniqVector<ui32>&& oldWatchDirs)
    : TGlob(fileConf, pattern, rootDir)
{
    WatchDirs = std::move(oldWatchDirs);
    MatchesHash = TString{hash};
}

void TGlob::ParseGlobPattern() {
    TVector<TStringBuf> parts;
    parts = StringSplitter(Pattern).Split(NPath::PATH_SEP).SkipEmpty();
    if (parts.empty()) {
        return;
    }

    auto hasRecursivePart = false;
    auto fixedPartBegin = parts.begin();
    auto finishFixedPart = [&](auto begin, auto end) {
        if (begin != end) {
            Parts.push_back({TGlobPart::EGlobType::Fixed, JoinStrings(begin, end, TStringBuf(&NPath::PATH_SEP, 1))});
        }
    };
    for (auto it = parts.begin(); it != parts.end(); it++) {
        TStringBuf part = *it;
        auto type = TGlobPart::EGlobType::Fixed;

        if (part == TStringBuf(&GLOB_ANY_STR, 1)) {
            type = TGlobPart::EGlobType::Anything;
        } else if (part == GLOB_ANY_RECURSIVE) {
            type = TGlobPart::EGlobType::Recursive;
            if (std::exchange(hasRecursivePart, true)) {
                throw yexception() << "only one recursive part is allowed";
            }
        } else if (part.Contains(GLOB_ANY_RECURSIVE)) {
            throw yexception() << "recursive part in pattern is not allowed";
        } else if (part.Contains(GLOB_ANY_STR) || part.Contains(GLOB_ANY_CHAR)) {
            type = TGlobPart::EGlobType::Pattern;
        }

        if (type != TGlobPart::EGlobType::Fixed) {
            finishFixedPart(fixedPartBegin, it);
            fixedPartBegin = std::next(it);

            if (type == TGlobPart::EGlobType::Pattern) {
                Parts.push_back({type, GlobToRegex(part)});
            } else {
                Parts.push_back({type, TString()});
            }
        }
    }
    finishFixedPart(fixedPartBegin, parts.end());
}

bool TGlob::WatchDirsUpdated(TFileConf& fileConf, const TUniqVector<ui32>& watchDirs) {
    return AnyOf(watchDirs, [&](ui32 dirId) {
        return fileConf.GetFileById(dirId)->CheckForChanges(ECheckForChangesMethod::RELAXED);
    });
}

bool TGlob::NeedUpdate(const TExcludeMatcher& excludeMatcher, TGlobStat* globStat) {
    TUniqVector<ui32> oldWatchDirs = std::move(WatchDirs);
    TString oldMatchesHash = std::move(MatchesHash);
    Apply(excludeMatcher, globStat);
    bool equal = (oldMatchesHash == MatchesHash) && (oldWatchDirs == WatchDirs);
    return !equal;
}

TVector<TFileView> TGlob::Apply(const TExcludeMatcher& excludeMatcher, TGlobStat* globStat) {
    using namespace NPath;
    WatchDirs.clear();
    TVector<TFileView> matches;
    TVector<TFileView> dirs;
    dirs.emplace_back(RootDir);
    size_t skippedFilesCount = 0;

    for (auto it = Parts.begin(); it != Parts.end(); it++) {
        const auto& part = *it;
        const auto isLastPart = it + 1 == Parts.end();

        TRegExMatch regex(part.Type == TGlobPart::EGlobType::Pattern ? part.Data : TString(), REG_NOSUB);
        auto match = [&part, &regex](TStringBuf name) {
            if (part.Type == TGlobPart::EGlobType::Anything) {
                return true;
            }
            if (part.Type == TGlobPart::EGlobType::Pattern) {
                return regex.Match(name.data());
            }

            return false;
        };

        TVector<TFileView> newDirs;
        for (const auto& dir : dirs) {
            switch (part.Type) {
                case TGlobPart::EGlobType::Fixed: {
                    auto path = NPath::Join(dir.GetTargetStr(), part.Data);
                    if (NeedFix(path)) {
                        path = Reconstruct(path);
                    }
                    auto id = FileConf.Add(path);
                    bool found = ApplyFixedPart(newDirs, matches, id, isLastPart, excludeMatcher, skippedFilesCount);
                    if (isLastPart || !found) {
                        WatchDirs.Push(FileConf.Add(NPath::Parent(path)));
                    }
                    break;
                }
                case TGlobPart::EGlobType::Pattern:
                case TGlobPart::EGlobType::Anything: {
                    WatchDirs.Push(dir.GetElemId());
                    ApplyPatternPart(newDirs, matches, match, dir.GetElemId(), isLastPart, excludeMatcher, skippedFilesCount);
                    if (!isLastPart) {
                        for (const auto& newDir : newDirs) {
                            WatchDirs.Push(newDir.GetElemId());
                        }
                    }
                    break;
                }
                case TGlobPart::EGlobType::Recursive: {
                    ApplyRecursivePart(newDirs, dir.GetElemId());
                    newDirs.push_back(dir);
                    for (const auto& newDir : newDirs) {
                        WatchDirs.Push(newDir.GetElemId());
                    }
                    break;
                }
                default:
                    break;
            }
        }
        dirs = newDirs;
    }

    if (WatchDirs.empty()) {
        WatchDirs.Push(RootDir.GetElemId());
    }

    MD5 md5;
    for (const auto& match : matches) {
        match.UpdateMD5(md5);
    }
    TMd5Sig sign;
    md5.Final(sign.RawData);
    MatchesHash = Md5SignatureAsBase64(sign);

    if (globStat) {
        globStat->MatchedFilesCount = matches.size();
        globStat->SkippedFilesCount = skippedFilesCount;
        globStat->WatchedDirsCount = WatchDirs.size();
    }

    return matches;
}

void TGlob::ApplyPatternPart(TVector<TFileView>& newDirs, TVector<TFileView>& matches, const std::function<bool(TStringBuf)>& matcher, ui32 dirId, const bool isLastPart, const TExcludeMatcher& excludeMatcher, size_t& skippedFilesCount) const {
    FileConf.ListDir(dirId, true);
    const auto& children = FileConf.GetCachedDirContent(dirId);
    for (const auto& childId : children) {
        auto file = FileConf.GetFileById(childId);
        const auto& data = file->GetFileData();
        if ((!data.IsDir && !isLastPart) || data.NotFound) {
            continue;
        }

        TFileView nameView = file->GetName();
        if (!matcher(nameView.Basename())) {
            if (!data.IsDir) ++skippedFilesCount;
            continue;
        }

        if (data.IsDir) {
            newDirs.push_back(nameView);
        } else if (!excludeMatcher.IsExcluded(nameView)) {
            matches.push_back(nameView);
        } else {
            ++skippedFilesCount;
        }
    }
}

bool TGlob::ApplyFixedPart(TVector<TFileView>& newDirs, TVector<TFileView>& matches, ui32 id, const bool isLastPart, const TExcludeMatcher& excludeMatcher, size_t& skippedFilesCount) const {
    auto file = FileConf.GetFileById(id);
    const auto& fileData = file->GetFileData();
    if (fileData.NotFound) {
        return false;
    }

    TFileView storedName = file->GetName();
    if (fileData.IsDir) {
        FileConf.ListDir(id, true);
        newDirs.push_back(storedName);
    } else if (isLastPart && !excludeMatcher.IsExcluded(storedName)) {
        matches.push_back(storedName);
    } else {
        ++skippedFilesCount;
    }
    return true;
}

void TGlob::ApplyRecursivePart(TVector<TFileView>& newDirs, ui32 startDirId) const {
    for  (TQueue<ui32> queue({startDirId}); !queue.empty(); queue.pop()) {
        auto dirId = queue.front();
        FileConf.ListDir(dirId, true);
        const auto& children = FileConf.GetCachedDirContent(dirId);
        for (const auto& childId : children) {
            auto file = FileConf.GetFileById(childId);
            const auto& data = file->GetFileData();
            if (!data.NotFound && data.IsDir) {
                TFileView name = file->GetName();
                newDirs.push_back(name);
                queue.push(childId);
            }
        }
    }
}

TString PatternToRegexp(TStringBuf pattern) {
    TStringBuilder res;
    res << REGEX_START;
    bool needSep = false;
    for (TStringBuf segment: StringSplitter(pattern).Split(NPath::PATH_SEP).SkipEmpty()) {
        if (std::exchange(needSep, true)) {
            res << NPath::PATH_SEP;
        }
        if (segment == GLOB_ANY_RECURSIVE) {
            res << '(' << REGEX_ANY_STR << NPath::PATH_SEP << ")?";
            needSep = false;
        } else {
            GlobSegmentToRegex(res, segment, false);
        }
    }
    res << REGEX_END;
    return res;
}

bool MatchPath(const TRegExMatch& globPattern, TFileView graphPath) {
    return globPattern.Match(TString{graphPath.CutAllTypes()}.c_str()); // TODO(svidyuk) search for regex lib which can work on TStringBuf in arcadia
}

void TExcludeMatcher::AddExcludePattern(TFileView moddir, TStringBuf pattern) {
    TString path;
    if (NPath::ToYPath(pattern, path)) {
        if (!NPath::IsType(path, NPath::Source)) {
            throw yexception() << "Only ${ARCADIA_ROOT} root is allowed in EXCLUDE pattern";
        }
    } else {
        path = NPath::Join(moddir.GetTargetStr(), pattern);
    }
    Matchers.push_back(PatternToRegexp(NPath::CutAllTypes(path)));
}

bool TExcludeMatcher::IsExcluded(TFileView path) const {
    return AnyOf(Matchers, [&](const TRegExMatch& pattern) {return MatchPath(pattern, path);});
}
