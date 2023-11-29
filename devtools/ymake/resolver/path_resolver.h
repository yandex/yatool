#pragma once

#include "resolve_ctx.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/common/iterable_tuple.h>

#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/strbuf.h>
#include <utility>

class TFileConf;

struct TResolveOptions {
    bool AllowDir       = true;  // Resolve both files and directories or just files
    bool AllowAbsRoot   = true;  // Try to convert absolute path to roots using Conf
    bool AllVariants    = false; // Collect all variants of resolving or just stop at first
    bool NoFS           = false; // Don't check FS for sources use only symbol table
    bool ResolveAsKnown = true;  // Call ResolveAsKnown as part if ResolveName

    void ResetToDefault() noexcept {
        *this = TResolveOptions();
    }
};

enum EResolveStatus {
    RESOLVE_SUCCESS = 0,         // Resolved as proper path relative to some root
    RESOLVE_FAILURE = 1,         // Not resolved at all
    RESOLVE_FAILURE_MISSING = 2, // Self-contained name is resolved to proper path, but the path is missing in FS or Graph
    RESOLVE_ERROR_ABSOLUTE = 17  // Resolved as absolute path
};

/// Class for direct access without strings by id to all required resolve elements
class TResolveFile : public TFileId {
public:
    TResolveFile()
        : TFileId()
        , Root_(NPath::ERoot::Unset)
    {}

    /// Root of resolve (source, build, unset)
    NPath::ERoot Root() const {
        return Root_;
    }

    /// Return two TStringBufs: linkPrefix and targetPath
    ///
    /// Non-copy return text information about resolve for output to stream
    /// or fast create string
    std::tuple<TStringBuf, TStringBuf> GetBufPair(const TFileConf& fileConf) const {
        return ELinkTypeHelper::LinkToTargetBufPair(GetLinkType(), GetTargetBuf(fileConf));
    }

    void Out(IOutputStream& os, const TFileConf& fileConf) const {
        auto [linkPrefix, targetPath] = GetBufPair(fileConf);
        os << linkPrefix << targetPath;
    }

    bool operator==(const TResolveFile& other) const {
        return (GetElemId() == other.GetElemId()) && (GetLinkType() == other.GetLinkType());
    }

private:
    NPath::ERoot Root_;

private:
    /// Assume that path is resolved without any checks
    explicit TResolveFile(TFileView fileView);

    /// Make resolved / unresolved from TStringBuf
    explicit TResolveFile(TFileConf& fileConf, TStringBuf name, ELinkType linkType, bool resolved);

    /// Return resolved as TString
    TString GetStr(TFileConf& fileConf) const {
        return ELinkTypeHelper::LinkToTargetString(GetLinkType(), GetTargetBuf(fileConf));
    }

    /// Return resolved target as TStringBuf
    TStringBuf GetTargetBuf(const TFileConf& fileConf) const {
        if (Empty()) {
            return TStringBuf();
        }
        return fileConf.RetBuf(GetTargetId());
    }

    friend class TPathResolver;
};

struct TResolveVariant {
    TResolveFile Path;
    TFileView Dir;
    const TFileConf& FileConf;

    // We care about unique paths, so we use them as discriminator
    bool operator==(const TResolveVariant& other) const {
        return Path == other.Path;
    }
    ui64 Hash() const {
        return IntHash(Path.GetElemId());
    }
};

template <>
struct THash<TResolveVariant> {
    size_t operator()(const TResolveVariant& variant) const {
        return variant.Hash();
    }
};

using TResolveVariants = TUniqDeque<TResolveVariant>;

/// @brief Resolve plan is tuple consisting of directories (TStringBuf) and directories sequences as iterator pairs
/// @example MakeResolvePlan(curDir, MakeIterPair(dirs), NPath::BldDir(), NPath::SrcDir())
template <typename... Dirs>
inline auto MakeResolvePlan(Dirs... dirs) {
    return std::make_tuple(dirs...);
}

/// @brief This is the resolver driver class
/// The source resolution is done by
/// - Name itself if it explicitly belongs to Source or Build tree
/// - Ordered set of directories supplied as resolvePlan
class TPathResolver {
public:
    explicit TPathResolver(TResolveContext& context, bool forcedListDir);

    /// @brief Resolve name to YPath either by name itself or in set of directories provided as resolvePlan
    /// @param curDir is used for ${CURDIR}/${BINDIR} substitution only, add it to buildPlan if needed
    /// @return status: success/failure/error
    template <typename TResolvePlan>
    EResolveStatus ResolveName(TStringBuf name, TFileView curDir, TResolvePlan resolvePlan);

    /// @brief Tries to construct YPath from the name using variable and configured roots
    /// @return true if result is valid YPath, false otherwise
    /// Also strips ./ and $U/ leaving stripped name in result and returning false
    EResolveStatus ResolveAsKnownWithCheck(TStringBuf name, TFileView curDir, TResolveFile& result) {
        SetName(name); // Canonizes name and save to Name_
        auto r = ResolveAsKnown(curDir, true);
        result = Result_;
        return r;
    }

    bool ResolveAsKnownWithoutCheck(TStringBuf name, TFileView curDir, TString& resultPath) {
        SetName(name); // Canonizes name and save to Name_
        auto r = ResolveAsKnown(curDir, false);
        resultPath = Name_;
        return r == RESOLVE_SUCCESS;
    }

    /// @brief get (possibly refined) name to which check were applied
    TStringBuf Name() const {
        return Name_;
    }

    /// @brief get context from name to which check were applied
    /// context extracted from name by GetContextFromLink or empty string
    ELinkType NameContext() const {
        return NameContext_;
    }

    /// @brief get YPath result of resolving
    TResolveFile Result() const {
        if ((!Result_.Empty()) && (NameContext_ != ELinkType::ELT_Default) &&
            (Result_.GetLinkType() != NameContext_)) {
            return AssumeResolved(Result_.GetTargetBuf(FileConf_), NameContext_);
        }
        return Result_;
    }

    /// @brief get directory that name was resolved to, if name was self-contained ResultDir() is empty.
    TFileView ResultDir() const {
        return ResultDir_;
    }

    /// Get all variants of resolving if requested by Options
    const TResolveVariants& Variants() const {
        return *Variants_;
    }

    /// Check that result is resolved outside of its directory (due to '..' in a name)
    bool IsOutOfDir() const;

    /// Check whether result of resolving is directory
    bool IsDir() const;

    /// @brief Get options for modification
    /// This also clears previous resolving results
    /// Not an overload to Options to avoid accidental Clear
    TResolveOptions& MutableOptionsWithClear() {
        ClearResults();
        return Options_;
    }

    /// Const version is just for options checking
    const TResolveOptions& Options() const {
        return Options_;
    }

    /// May be resolved from TFileView
    TResolveFile AssumeResolved(const TFileView fileView) const {
        return TResolveFile(fileView);
    }

    /// Assume that path is resolved without any checks
    TResolveFile AssumeResolved(const TStringBuf name, ELinkType linkType = ELT_Default) const {
        if (name.Empty()) {
            return TResolveFile();
        }
        return TResolveFile(FileConf_, name, linkType, true);
    }

    /// Make unresolved from TStringBuf
    TResolveFile MakeUnresolved(const TStringBuf name, ELinkType linkType = ELT_Default) const {
        if (name.Empty()) {
            return TResolveFile();
        }
        return TResolveFile(FileConf_, name, linkType, false);
    }

    /// Return two TStringBufs: linkPrefix and targetPath
    ///
    /// Non-copy return text information about resolve for output to stream
    /// or fast create string
    std::tuple<TStringBuf, TStringBuf> GetBufPair(const TResolveFile resolveFile) const {
        return resolveFile.GetBufPair(FileConf_);
    }

    /// Return resolved as TString
    TString GetStr(const TResolveFile resolveFile) const {
        return resolveFile.GetStr(FileConf_);
    }

    /// Return Result as TString
    TString GetResultStr() const {
        return Result().GetStr(FileConf_);
    }

    /// Return resolved target as TStringBuf
    TStringBuf GetTargetBuf(const TResolveFile resolveFile) const {
        return resolveFile.GetTargetBuf(FileConf_);
    }

    /// Return Result target as TStringBuf
    TStringBuf GetResultTargetBuf() const {
        return Result().GetTargetBuf(FileConf_);
    }
private:
    /// @brief tries to check Name by its own
    /// Result contains either resolution on success or empty string on failure
    EResolveStatus ResolveAsKnown(TFileView curDir, bool check);

    /// @brief Canonizes name and save to Name_
    /// This also clears previous resolving results
    void SetName(TStringBuf name);

    /// Returns whether resolution was successful (only for check mode)
    bool IsResolved() const {
        return !Result_.Empty();
    }

    /// Clear results is not needed
    void ClearResults() {
        Result_ = {};
        ResultDir_ = {};
        Variants_.Reset();
    }

    /// Main check function: checks file against Build or Source root
    /// It is expected that dir is valid YPath.
    /// Outputs: returns true upon success, false on failure;
    TResolveFile CheckByRoot(TFileView dir);

    /// Check path in build root via Module and Graph
    TResolveFile CheckBuilt(TStringBuf path);

    /// Check path in source root via Symbol Table and FS
    ///
    /// Upon checking of FS cache entire parent directory to accelerate
    /// successive checks within same directory.
    /// This tries to balance deep caching from root and not caching at all used previously
    TResolveFile CheckSourceCacheParent(TFileView dir, TStringBuf path, bool wasReconstructed);

    /// Check existence of first not common parent (or path) in Symbol Table
    ///
    /// Return <Success, TFileView of path>
    /// Success = false, if absent first non-common parent or type of path is invalid
    /// Success = true, if first non-common parent or path valid, if TFileView empty - require recache direct parent of path and try again
    std::tuple<bool, TResolveFile> CheckFirstParent(TFileView dir, TStringBuf path, bool wasReconstructed);

    /// The function called from tuple iterator
    /// It calls CheckByRoot() and combines results of multiple invocations
    /// - It decides based on options and results whether to proceed or to stop
    /// - It accumulates all variants of resolving if Options.AllVariants is set
    bool CheckDirectory(TFileView dir);

    friend struct TIterTupleChecker<TFileView, TPathResolver>;
    friend struct NIterTupleDetail::TIterTupleCheckerImpl<TFileView, TPathResolver>;

    /// @brief Checking functor for TIterTupleChecker: checks Name in dir and accumulates results
    bool operator()(TFileView dir) {
        return CheckDirectory(dir);
    }

    /// Locate path in symbol table and check its presence and whether it is directory
    /// Return <resolve completed, fileview if resolve success> for path
    std::tuple<bool, TResolveFile> CheckSourceById(TStringBuf path, bool makeUpToDate = false) const;

    /// Call TFileConf::ListDir inside resolve specific settings
    void ListDir(TStringBuf dir) const;

private:
    // Inputs
    TResolveContext& Context_;
    const bool ForcedListDir_;
    TFileConf& FileConf_;
    TResolveOptions Options_;

    // Input/output
    TString Name_;           // Name to be checked (refined in SetName())
    ELinkType NameContext_;  // Name's context
    TString TempStr_;        // Temporary TString for use in places, where TString as buffer require

    // Results
    TResolveFile Result_;
    TFileView ResultDir_;
    THolder<TResolveVariants> Variants_;

    // Scratch
    bool NeedFix_ = true;
};

template <typename TResolvePlan>
EResolveStatus TPathResolver::ResolveName(TStringBuf name, TFileView curDir, TResolvePlan resolvePlan) {
    SetName(name); // Canonizes name and save to Name_
#ifdef NDEBUG
    // In DEBUG mode we control that ResolveAsKnown is inapplicable
    // if it is turned off. See Y_ASSERT below.
    if (Options().ResolveAsKnown) {
#endif
        auto r = ResolveAsKnown(curDir, true);
        if ((r == RESOLVE_SUCCESS) || (r == RESOLVE_FAILURE_MISSING)) {
            Y_ASSERT(Options().ResolveAsKnown);
            return r;
        } else if (NPath::TSplitTraits::IsAbsolutePath(name)) {
            return RESOLVE_ERROR_ABSOLUTE;
        }
#ifdef NDEBUG
    }
#endif
    // Check against directories in resolvePlan
    TIterTupleChecker<TFileView, TPathResolver> checker(*this);
    checker.Run(resolvePlan); // This may return false if we hunt for all resolving variants
    return IsResolved() ? RESOLVE_SUCCESS : RESOLVE_FAILURE;
}
