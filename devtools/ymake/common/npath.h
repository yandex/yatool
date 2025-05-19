#pragma once
#include "path_definitions.h"

#include <devtools/ymake/diag/dbg.h>

#include <util/folder/path.h>
#include <util/folder/pathsplit.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/yexception.h>
#include <util/string/join.h>
#include <util/string/split.h>
#include <util/string/subst.h>
#include <util/system/defaults.h>


// Interface in global namespace
TString ArcPath(TStringBuf path);
TString BuildPath(TStringBuf path);
bool InSrcDir(TStringBuf path);
bool InBuildDir(TStringBuf path);

// path representation, stores root:/relative to save memory - it should be late resolved by TBuildConfiguration instance
namespace NPath {

using TSplitTraits = TPathSplitTraitsLocal;
using TSplit = TPathSplitUnix;

// returns non-typed path
inline TString FromLocal(TStringBuf path) {
    TPathSplit localPathSplit(path);
    if (localPathSplit.IsAbsolute) {
        throw yexception() << "Local path must be relative";
    }
    TString res = localPathSplit.Reconstruct();
    if (PATH_SEP != TPathSplitTraitsLocal::MainPathSep) {
        SubstGlobal(res, TPathSplitTraitsLocal::MainPathSep, PATH_SEP);
    }
    return res;
}

// returns non-typed path
inline TString FromLocal(const TFsPath& path) {
    return FromLocal(TStringBuf(path.c_str()));
}

constexpr TStringBuf SrcDir() noexcept {
    return SRC_DIR;
}

constexpr TStringBuf BldDir() noexcept {
    return BLD_DIR;
}

constexpr TStringBuf UnkDir() noexcept {
    return UNK_DIR;
}

constexpr TStringBuf DummyFile() noexcept {
    return DUMMY_FILE;
}

Y_FORCE_INLINE bool IsExternalPath(TStringBuf str) {
    const TStringBuf parent = str.Before(PATH_SEP);
    return parent.size() > 3 && parent.at(0) == SPECSYM && parent.at(1) == EXT_PATH_L_DELIM && parent.back() == EXT_PATH_R_DELIM;
}

Y_FORCE_INLINE bool IsTypedPath(TStringBuf str) {
    return str.size() > 1 && str.at(0) == SPECSYM;
}
Y_FORCE_INLINE bool IsTypedPathEx(TStringBuf str) {
    bool validLongPath = str.size() > ROOT_LENGTH ? str.at(ROOT_LENGTH) == PATH_SEP : true;
    return IsTypedPath(str) && validLongPath && EqualToOneOf(str.at(1), ENUM_TO_TYPE[Source], ENUM_TO_TYPE[Build], ENUM_TO_TYPE[Unset], ENUM_TO_TYPE[Link]);
}

Y_FORCE_INLINE void Validate(TStringBuf str) {
    Y_ASSERT(IsTypedPath(str));
}
Y_FORCE_INLINE void ValidateEx(TStringBuf str) {
    Y_ASSERT(IsTypedPathEx(str) || IsExternalPath(str));
}

inline bool IsType(TStringBuf str, ERoot root) {
    Validate(str);
    return str.at(1) == ENUM_TO_TYPE[root];
}

inline TString SetType(TStringBuf str, ERoot root) {
    Y_ENSURE(!IsType(str, Link));
    Validate(str);
    char type[] = {SPECSYM, ENUM_TO_TYPE[root], '\0'};
    return TString::Join(type, str.substr(2));
}

inline ERoot GetType(TStringBuf str) {
    Validate(str);
    if (str.at(1) == ENUM_TO_TYPE[Source])
        return Source;
    if (str.at(1) == ENUM_TO_TYPE[Build])
        return Build;
    if (str.at(1) == ENUM_TO_TYPE[Unset])
        return Unset;
    if (str.at(1) == ENUM_TO_TYPE[Link])
        return Link;

    throw yexception() << "Unknown type in NPath::GetType: " << str << Endl;
}

inline TStringBuf GetTypeStr(TStringBuf path) {
    Validate(path);
    return TStringBuf(path, 0, ROOT_LENGTH);
}

inline TStringBuf CutType(TStringBuf str) {
    Y_ENSURE(!IsType(str, Link));
    Validate(str);
    return str.SubStr(PREFIX_LENGTH);
}

inline TStringBuf CutAllTypes(TStringBuf str) {
    while (IsTypedPath(str)) {
        Y_ENSURE(!IsType(str, Link));
        str = str.SubStr(PREFIX_LENGTH);
    }
    return str;
}

inline bool IsRoot(TStringBuf path) {
    Validate(path);
    return path.length() == ROOT_LENGTH;
}

inline bool IsLink(TStringBuf name) {
    return IsTypedPath(name) && IsType(name, Link);
}

inline TStringBuf GetTargetFromLink(TStringBuf link) {
    Y_ENSURE(IsType(link, Link));
    auto pos = link.find(SPECSYM, 1);
    Y_ENSURE(pos != link.npos);
    return link.SubStr(pos);
}

inline TStringBuf GetContextFromLink(TStringBuf link) {
    Y_ENSURE(IsType(link, Link));
    return link.Skip(PREFIX_LENGTH).Before(SPECSYM).Chop(1);
}

inline TStringBuf ResolveLink(TStringBuf name) {
    if (IsLink(name)) {
        return GetTargetFromLink(name);
    }
    return name;
}

inline bool IsBuildLink(TStringBuf name) {
    if (!IsLink(name)) {
        return false;
    }
    return GetType(GetTargetFromLink(name)) == ERoot::Build;
}

Y_FORCE_INLINE bool SkipType(ERoot type, TStringBuf& path) {
    if (path.size() > ROOT_LENGTH && path.at(ROOT_LENGTH) == PATH_SEP && IsTypedPath(path) && path.at(1) == ENUM_TO_TYPE[type]) {
        path = path.Skip(ROOT_LENGTH + 1);
        return true;
    }
    return false;
}

Y_FORCE_INLINE bool IsKnownRoot(TStringBuf path) {
    SkipType(ERoot::Unset, path);
    return IsTypedPath(path) && (IsType(path, ERoot::Build) || IsType(path, ERoot::Source));
}

inline TStringBuf Parent(TStringBuf path) {
    size_t slash = path.rfind(PATH_SEP);
    if (slash == TString::npos) {
        return "";
    }
    return TStringBuf(path, 0, slash);
}

inline TStringBuf Basename(TStringBuf path) {
    size_t slash = path.rfind(PATH_SEP);
    if (slash == TString::npos) {
        return path;
    }
    return TStringBuf(path, slash + 1, TStringBuf::npos);
}

inline TStringBuf AnyBasename(TStringBuf path) {
    size_t slash = path.rfind(PATH_SEP);
    size_t bslash = path.rfind(WIN_PATH_SEP);
    if (slash == TString::npos && bslash == TString::npos) {
        return path;
    } else if (slash == TString::npos) {
        slash = bslash;
    }
    return TStringBuf(path, slash + 1, TStringBuf::npos);
}

inline TStringBuf NoExtension(TStringBuf path) {
    size_t dot = path.rfind('.');
    if (dot != TString::npos)
        path = TStringBuf(path, 0, dot);
    return path;
}

inline TStringBuf Extension(TStringBuf path) {
    size_t dot = path.rfind('.');
    if ((dot != TString::npos) && (dot != path.size() - 1))
        path = TStringBuf(path, dot + 1, TStringBuf::npos);
    return path;
}

inline TStringBuf BasenameWithoutExtension(TStringBuf path) {
    size_t dot = path.rfind('.');
    size_t slash = path.rfind(PATH_SEP);
    if (dot != TString::npos && slash != TString::npos && dot > slash) {
        return TStringBuf(path, slash + 1, dot - slash - 1);
    } else if (slash != TString::npos) {
        return TStringBuf(path, slash + 1, TStringBuf::npos);
    } else if (dot != TString::npos) {
        Y_ASSERT(path.at(0) != SPECSYM);
        return TStringBuf(path, 0, dot);
    }

    Y_ASSERT(path.at(0) != SPECSYM);
    return path;
}

// Note: copied and rewritten to better use TPathSplit from TFsPath
// TODO: move to util, templated by split type
inline TString Relative(TStringBuf path, TStringBuf root) {
    TSplit pathSplit(path);
    TSplit rootSplit(root);
    size_t prefixSize = 0;
    while (pathSplit.size() > prefixSize && rootSplit.size() > prefixSize && pathSplit[prefixSize] == rootSplit[prefixSize]) {
        ++prefixSize;
    }
    if (!prefixSize && !(pathSplit.IsAbsolute && rootSplit.IsAbsolute)) {
        ythrow yexception() << "No common parts in " << path << " and " << root;
    }
    TSplit split;
    for (size_t i = prefixSize; i < rootSplit.size(); ++i) {
        split.ParsePart("..");
    }
    for (size_t i = prefixSize; i < pathSplit.size(); ++i) {
        split.ParsePart(pathSplit[i]);
    }
    return split.Reconstruct();
}

// omit heavy fspath for simple operations (two overloads to fix ambiguity).
template<typename ...Ts>
TString Join(Ts&&... items) {
    return ::Join(PATH_SEP_S, std::forward<Ts>(items)...);
}

inline bool NeedFix(TStringBuf s) {
/*
    return s.EndsWith("/.") || s.find("..") != TString::npos || s.find("/./") != TString::npos || s.find("//") != TString::npos;
    Single pass copy of condition below
*/
    const auto* p = s.data();
    const auto* pe1 = p + s.size() - 1;
    while (p < pe1) {
        auto c = *p++;
        if ('/' == c) { // '/' is most frequent char that '.', check it first
            auto c1 = *p;
            if ('/' == c1) {
                return true; // find("//")
            } else if ('.' == c1) {
                if (p == pe1) {
                    return true; // EndWith("/.")
                }
                auto c2 = *(p + 1);
                if ('/' == c2) {
                    return true; // find("/./")
                } else if ('.' == c2) {
                    return true; // find("..")
                }
                p += 2; // (*p == '.') but (*(p + 1) != '/') && (*(p + 1) != '.'), skip both
            } else {
                ++p; // (*p != '/') && (*p != '.') - skip this char
            }
            continue;
        }
        if (('.' == c) && ('.' == *p)) {
            return true; // find("..")
        }
    }
    return false;
}

inline TString ConstructPath(TStringBuf str, ERoot root = Unset) {
    Y_ENSURE(!IsLink(str));
    char type[] = { SPECSYM, ENUM_TO_TYPE[root], '\0' };
    if (!str || str == ".") {
        return TString(type);
    }
    return TString::Join(type, PATH_SEP_S, str);
}

inline TString SmartJoin(TStringBuf path, TStringBuf file) {
    Validate(path);
    if (!file) {
        return TString(path);
    }
    TStringBuf cutPath = CutType(path);
    return ConstructPath(TSplit(cutPath).ParsePart(file).Reconstruct(), GetType(path));
}

inline TString GenPath(TStringBuf dirname, TStringBuf name) {
    return SmartJoin(dirname, name);
}

// same as GenPath but always places given root (Source or Build)
inline TString GenTypedPath(TStringBuf dirname, TStringBuf name, ERoot root) {
    return SetType(SmartJoin(dirname, name), root);
}

// same as GenPath but always places $S
inline TString GenSourcePath(TStringBuf dirname, TStringBuf name) {
    return GenTypedPath(dirname, name, Source);
}

// same as GenPath but always places $B
inline TString GenBuildPath(TStringBuf dirname, TStringBuf name) {
    return GenTypedPath(dirname, name, Build);
}

namespace NDetail {
    struct TVarPathPrefixDescriptor {
        TStringBuf Value;              // The var reference e.g. ${ARCADIA_ROOT}
        size_t     DiscriminatingPos;  // Position of discriminating char. Prefer power of 2
        ERoot      Root;
        bool       NeedDir;
    };

    constexpr const std::array<TVarPathPrefixDescriptor, 4> VarPathPrefixes {{
        // Basic vars (NeedPrefix == false) go here
        {"${ARCADIA_ROOT}"sv, 12, Source, false},
        {"${ARCADIA_BUILD_ROOT}"sv, 12, Build, false},
        // Extended vars (NeedPrefix == true) go here
        {"${CURDIR}"sv, 4, Source, true},
        {"${BINDIR}"sv, 4, Build, true}
    }};
    const size_t VarPathPrefixesCountWithDir = VarPathPrefixes.size();
    const size_t VarPathPrefixesCountWoDir = 2;

    static_assert(VarPathPrefixes[0].Value[VarPathPrefixes[0].DiscriminatingPos] == 'O');
    static_assert(VarPathPrefixes[1].Value[VarPathPrefixes[1].DiscriminatingPos] == 'I');
    static_assert(VarPathPrefixes[2].Value[VarPathPrefixes[2].DiscriminatingPos] == 'R');
    static_assert(VarPathPrefixes[3].Value[VarPathPrefixes[3].DiscriminatingPos] == 'N');

    inline const TVarPathPrefixDescriptor* ClassifyPath(TStringBuf path, bool hasDir) {
        size_t cnt = hasDir ? NDetail::VarPathPrefixesCountWithDir : NDetail::VarPathPrefixesCountWoDir;
        for (size_t i = 0; i < cnt; ++i) {
            auto res = &NDetail::VarPathPrefixes[i];
            if (path.size() >= res->Value.size() && path[res->DiscriminatingPos] == res->Value[res->DiscriminatingPos]) {
                return path.StartsWith(res->Value) ? res : nullptr;
            }
        }
        return nullptr;
    }

}

/// Check whether path start with vars designating known full path, like `${ARCADIA_ROOT}`, `${ARCADIA_BUILD_ROOT}`
/// If curDir is provided also handle `${CURDIR}` and `${BINDIR}`
inline bool IsKnownPathVarPrefix(TStringBuf path, bool withCurDir = true) {
    return NDetail::ClassifyPath(path, withCurDir) != nullptr;
}

/// Process vars designating known full path prefix `${ARCADIA_ROOT}`, `${ARCADIA_BUILD_ROOT}`
/// If curDir is provided also handle `${CURDIR}` and `${BINDIR}`
///
/// @param path - the path to process
/// @param result - resuling path string with internal prefixes (`$S`/`$B`)
/// @param curDir - used as prefix construct references to `${CURDIR}` and `${BINDIR}`
inline bool ToYPath(TStringBuf path, TString& result, TStringBuf curDir={}) {
    const auto* varPathPrefix = NDetail::ClassifyPath(path, !curDir.empty());
    if (varPathPrefix != nullptr) {
        TStringBuf bldFile = path;
        bldFile.Skip(varPathPrefix->Value.size());
        while (bldFile && bldFile.front() == PATH_SEP) {
            bldFile.Skip(1);
        }
        while (bldFile && bldFile.back() == PATH_SEP) {
            bldFile.Chop(1);
        }
        if (varPathPrefix->NeedDir) {
            result = GenTypedPath(curDir, bldFile, varPathPrefix->Root);
        } else {
            result = ConstructPath(bldFile, varPathPrefix->Root);
        }
        return true;
    }
    return false;
}

inline TString Reconstruct(TStringBuf path) {
    Validate(path);
    TString resPath = TPathSplitUnix(path).Reconstruct();
    if (!IsTypedPath(resPath))
        resPath = NPath::ConstructPath("../", NPath::GetType(path)) + resPath;
    return resPath;
}

/// Checking path out of tree after Reconstruct, for example, $S/../filename
inline bool IsTypedOutOfTree(TStringBuf path) {
    return (path.size() >= (2/*root*/ + 1/*slash*/ + 2/*dots*/ + 1/*slash*/)) && ("/../" == path.substr(2, 4));
}

// TODO: check if faster than SmartJoin
inline TString SmarterJoin(TStringBuf path, TStringBuf file) {
    Validate(path);
    if (!file || (file == ".")) {
        return TString(path);
    }
    TStringBuf cutPath = CutType(path);
    if (!cutPath || (cutPath == ".") || (cutPath == "./")) {
        return ConstructPath(file, GetType(path));
    }
    if (path.back() == PATH_SEP) {
        return TString::Join(path, file);
    }
    return TString::Join(path, PATH_SEP_S, file);
}

inline TString GenResultPath(TStringBuf module, TStringBuf origin) {
    TString result(module);
    size_t slash = origin.rfind(PATH_SEP);
    if (slash != TString::npos) {
        result += origin.substr(slash);
    }
    return result;
}

enum class EDirConstructIssue {
    ExtraSep,
    TrailingSep,
    SourceDir,
    EmptyDir
};

/// Try to convert directory to normalized and valid Arcadia path
/// @param arg - path relative to prefix
/// @param prefix - path from arcadia root to some directory
/// @param diag - Diagnostic callback allowing in-line issues reporting (warnings)
template<typename TDiagHandler>
TString ConstructYDir(TStringBuf arg, TStringBuf prefix, TDiagHandler diag) {
    TString dir;
    TStringBuf argCopy = arg;
    bool dirResolved = ToYPath(argCopy, dir);
    if (!dirResolved) {
        while (!argCopy.empty() && argCopy.at(0) == PATH_SEP) {
            diag(EDirConstructIssue::ExtraSep, argCopy);
            argCopy.Skip(1);
        }
        while (!argCopy.empty() && argCopy.back() == PATH_SEP) {
            diag(EDirConstructIssue::TrailingSep, argCopy);
            argCopy.Chop(1);
        }

        if (argCopy.empty() || argCopy == ".") {
            diag(argCopy.empty() ? EDirConstructIssue::EmptyDir : EDirConstructIssue::SourceDir, argCopy);
            return "";
        }

        // TODO: try simple Join
        dir = ArcPath(prefix.size() ? TSplit(prefix).ParsePart(argCopy).Reconstruct() : argCopy);
    }

    if (dir.empty() || dir == SrcDir()) {
        diag(dir.empty() ? EDirConstructIssue::EmptyDir : EDirConstructIssue::SourceDir, dir);
        return "";
    }
    if (NeedFix(dir)) {
        dir = Reconstruct(dir);
    }
    return dir;
}

// This reconstructs TPathSplit into Unix style path on Windows
struct TUnixPath : protected TPathSplit {
    TUnixPath(const TPathSplit& split)
        : TPathSplit(split)
    {
    }

    TUnixPath(TPathSplit&& split)
        : TPathSplit(split)
    {
    }

    TUnixPath(TStringBuf loc)
        : TPathSplit(loc)
    {
    }

    TString Reconstruct() const {
        return DoReconstruct(PATH_SEP_S);
    }
};

inline bool HasWinSlashes(TStringBuf path) {
    return path.find(WIN_PATH_SEP) != TString::npos;
}

inline TString NormalizeSlashes(TStringBuf path) {
    TString res;
    if (!TSplitTraits::IsPathSep('\\')) {
        res = path;
    } else {
        res = TUnixPath(path).Reconstruct();
    }
    return res;
}

inline bool IsPrefixOf(TStringBuf prefix, TStringBuf path) noexcept {
    if (!prefix.empty() && prefix.back() == NPath::PATH_SEP) {
        prefix.Chop(1);
    }
    if (path.size() < prefix.size()) {
        return false;
    }
    if (path.size() > prefix.size() && path[prefix.size()] != NPath::PATH_SEP) {
        return false;
    }
    const TStringBuf pathPrefix = path.Head(prefix.size());
    return Equal(
        std::make_reverse_iterator(prefix.end()), std::make_reverse_iterator(prefix.begin()),
        std::make_reverse_iterator(pathPrefix.end()), std::make_reverse_iterator(pathPrefix.begin()));
}

inline bool IsExplicitDirectory(TStringBuf name) noexcept {
   return name.EndsWith(PATH_SEP) || name.EndsWith(WIN_PATH_SEP);
}

inline TStringBuf CutPoint(TStringBuf path) {
    if (path.StartsWith("./") && path.size() > 2) {
        path.Skip(2);
        if (path.front() == NPath::PATH_SEP) {
            path.Skip(1);
        }
    }
    return path;
}

inline TStringBuf PreparePath(TStringBuf path) {
    if (IsTypedPathEx(path) && IsType(path, ERoot::Unset)) {
        path = CutType(path);
    }
    return CutPoint(path);
}

inline bool MustDeepReplace(TStringBuf path) {
    if (IsLink(path)) {
        return true;
    }
    path = PreparePath(path);
    TString s;
    if (HasWinSlashes(path)) {
        s = NormalizeSlashes(path);
        path = s;
    }
    return !IsTypedPathEx(path) && !IsKnownPathVarPrefix(path, true);
}

} // NPath

/// translate short canonical arcadia path to NPath (add root)
inline TString ArcPath(TStringBuf path) {
    return NPath::ConstructPath(path, NPath::Source);
}

inline TString BuildPath(TStringBuf path) {
    return NPath::ConstructPath(path, NPath::Build);
}

inline bool InSrcDir(TStringBuf path) {
    return path.StartsWith(SRC_DIR);
}

inline bool InBuildDir(TStringBuf path) {
    return path.StartsWith(BLD_DIR);
}
