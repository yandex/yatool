#pragma once

#include "module_state.h"
#include "module_add_data.h"
#include "macro_vars.h"

#include <devtools/ymake/resolver/path_resolver.h>
#include <devtools/ymake/resolver/resolve_cache.h>

#include <util/generic/vector.h>
#include <util/generic/string.h>
#include <util/generic/strbuf.h>

struct TInclude;
class TUpdIter;

struct TModuleResolveContext : TResolveContext {
    TResolveCache ResolveCache;
    NStats::TResolveStats& ResolveStats;

    TModuleResolveContext(const TRootsOptions& conf, TDepGraph& graph, const TOwnEntries& ownEntries,
                          const TParsersCache& parsersCache, TResolveCache resolveCache,
                          NStats::TResolveStats& resolveStats,
                          NGraphUpdater::TNodeStatusChecker nodeStatusChecker = {})
    : TResolveContext(conf, graph, ownEntries, parsersCache, nodeStatusChecker)
    , ResolveCache(resolveCache)
    , ResolveStats(resolveStats)
    {
    }
    TModuleResolveContext(const TModuleResolveContext&) = default;
};


TModuleResolveContext MakeModuleResolveContext(const TModule& mod, const TRootsOptions& conf, TDepGraph& graph, const TUpdIter& updIter,
                                               const TParsersCache& parsersCache); // Defined in add_iter.cpp due to TUpdIter access


/// @brief This class provides name-resolving facilities in context of a module
class TModuleResolver {
protected:
    TModule& Module;
    const TBuildConfiguration& Conf;

private:
    mutable TModuleResolveContext Ctx;
    mutable THolder<TPathResolver> _Resolver;
    mutable TString _TempStr; // Temporary TString for use in places, where TString as buffer require
    TString _SrcName; // Temporary TString for use in ResolveSingleInclude

    static const constexpr TLangId BY_SRC = TModuleIncDirs::BAD_LANG;

public:
    TModuleResolver(TModule& module, const TBuildConfiguration& conf, TModuleResolveContext ctx)
        : Module(module)
        , Conf(conf)
        , Ctx(ctx)
    {
    }

    enum EResolveKind {
        ArcadiaRoot,
        Local,
        Sysincl,
        Addincl,
    };

    /// Policy for unresolved event
    enum class EResolveFailPolicy: ui32 {
        Default = 0b00, // Don't create $U path (return empty string and ElemId=0)
        LastTry = 0b01, // Create $U path (and ElemId as index in NameStore for it)
        Silent = 0b10,  // Used only if LastDry enabled! Skip creating configure error
    };
    using enum EResolveFailPolicy;

    /// This resolves set of includes
    /// Note: if some include is ignored by sysincls.lst it won't have matching entry in the result
    void ResolveIncludes(TFileView src, const TVector<TInclude>& includes, TVector<TResolveFile>& result, TLangId langId = BY_SRC);

    /// This resolves set of includes treating all them as local
    /// Note: if some include is ignored by sysincls.lst it won't have matching entry in the result
    void ResolveLocalIncludes(TFileView src, const TVector<TString>& includes, TVector<TResolveFile>& result, TLangId langId = BY_SRC);
    void ResolveLocalIncludes(TFileView src, const TVector<TStringBuf>& includes, TVector<TResolveFile>& result, TLangId langId = BY_SRC);

    /// This resolves include file as included from src and appends it to result vector.
    /// Directory of src, arcadia root, sysincls and IncDirs are used to search for name.
    /// Particular order depends on whether include is system or local.
    /// Possible appends to result vector:
    /// * Multiple or zero entries obtained from sysincl for single include
    /// * $U-rooted paths when some includes have not been resolved
    void ResolveSingleInclude(TFileView src, const TInclude& include, TVector<TResolveFile>& result, TLangId langId = BY_SRC);

    /// Resolve name as source: use SrcDirs to search and validate presence of resolved name
    /// In case of success returns valid YPath in either source or build tree.
    /// In case of failure result is either empty or $U-rooted only depending on failPolicy setting
    /// If LastTry is without Silent in case of failure NEvent::TInvalidFile event is sent with SrcDirs as Dirs
    TResolveFile ResolveSourcePath(TStringBuf name, TFileView curDir, EResolveFailPolicy failPolicy, bool allowDir = true);

    /// Resolve name in var.Name as source: use SrcDirs to search and validate presence of resolved name
    /// If no valid resolving found returns false. src.Name is updated to $U-rooted only if failPolicy is true
    /// If LastTry is without Silent in case of failure NEvent::TInvalidFile event is sent with SrcDirs as Dirs
    bool ResolveSourcePath(TVarStrEx& src, TFileView curDir, EResolveFailPolicy failPolicy, bool allowDir = true);

    /// Resolve name in var.Name as induced dependency: use SrcDirs and IncDirs to search and validate presence of resolved name
    /// If no valid resolving found returns false. src.Name is updated to $U-rooted only if lastTry is true
    /// @note: EResolveFailPolicy::Silent is always added to the user provided failPolicy
    bool ResolveInducedDepPath(TVarStrEx& src, TFileView curDir, EResolveFailPolicy failPolicy);

    /// Resolve name as output of another command: relocate it to build tree and validate its presence
    /// In case of success returns valid YPath in build tree.
    /// In case of failure result is $U-rooted `name`
    TResolveFile ResolveAsOutput(TStringBuf name);

    // Make |path| to be "incorporated" (==localized) by a module.
    TString ResolveToModuleBinDirLocalized(TStringBuf path);

    /// Check that path is known path with checking of path via FS or graph
    EResolveStatus ResolveAsKnownWithCheck(TStringBuf relativePath, TResolveFile& result) const {
        return Resolver().ResolveAsKnownWithCheck(relativePath, Module.GetDir(), result);
    }

    /// Check that path is known path (doesn't require actual resolving, without checking of path via FS or graph)
    bool ResolveAsKnownWithoutCheck(TStringBuf relativePath, TString& result) const {
        return Resolver().ResolveAsKnownWithoutCheck(relativePath, Module.GetDir(), result);
    }

    /// Check that path is known path (doesn't require actual resolving)
    /// In case of success Name of the src is updated.
    bool ResolveAsKnownWithoutCheck(TVarStrEx& src) {
        // src.Name will be overwrited at return only, we can use it as input and as output safely
        return src.IsPathResolved = ResolveAsKnownWithoutCheck(src.Name, src.Name);
    }

    /// Create $U-rooted `name` and output NEvent::TInvalidFile event using SrcDirs
    void ReportSourcePathAsUnset(TStringBuf name) const;

    /// This creates build path to introduce Outputs of commands according to var properties
    /// It performs only checking against trailing '/' to prevent obvious directories since
    /// BuildPath is always assumed a file. In case of success it returns $B-rooted path
    /// in var and true otherwise var is unchanged and false is returned
    bool FormatBuildPath(TVarStrEx& var, TFileView srcDir, TFileView buildDir);

    const TModule& GetModule() const {
        return Module;
    }

    TModule& GetModule() {
        return Module;
    }

    const TBuildConfiguration& GetConf() const {
        return Conf;
    }

    /// Assume that path is resolved without any checks
    TResolveFile AssumeResolved(const TStringBuf name, ELinkType linkType = ELT_Default) {
        return Resolver().AssumeResolved(name, linkType);
    }

    /// Make unresolved from TStringBuf
    TResolveFile MakeUnresolved(const TStringBuf name, ELinkType linkType = ELT_Default) const {
        return Resolver().MakeUnresolved(name, linkType);
    }

    /// Return two TStringBufs: linkPrefix and targetPath
    std::tuple<TStringBuf, TStringBuf> GetBufPair(const TResolveFile resolveFile) const {
        return Resolver().GetBufPair(resolveFile);
    }

    /// Return resolved as TString
    TString GetStr(const TResolveFile resolveFile) const {
        return Resolver().GetStr(resolveFile);
    }

    /// Return Result as TString
    TString GetResultStr() const {
        return Resolver().GetResultStr();
    }

    /// Return resolved target as TStringBuf
    TStringBuf GetTargetBuf(const TResolveFile resolveFile) const {
        return Resolver().GetTargetBuf(resolveFile);
    }

    /// Return Result target as TStringBuf
    TStringBuf GetResultTargetBuf() const {
        return Resolver().GetResultTargetBuf();
    }

    /// Bulk make unresolved from includes
    template<class TIncl>
    void ResolveAsUnset(const TVector<TIncl>& includes, TVector<TResolveFile>& resolved);

    TPathResolver& Resolver() const {
        if (!_Resolver) {
            _Resolver = MakeHolder<TPathResolver>(Ctx, Conf.ShouldForceListDirInResolving());
        }
        return *_Resolver;
    }

protected:
    /// Make $U-path out of name and post TInvalidFile even enlisting passed directories
    template <typename TDirList>
    void ReportPathAsUnset(TStringBuf name, TDirList&& dirs) const;

    template <typename TDirList>
    bool ResolvePath(TVarStrEx& src, TFileView curDir, TDirList&& dirs, EResolveFailPolicy failPolicy, bool allowDir = true);

    /// Make resolving of name in curDir and dirs,return ElemId of resolving
    /// For unresolved ElemId filled by EResolveFailPolicy
    template <typename TDirList>
    TResolveFile ResolvePath(TStringBuf name, TFileView curDir, TDirList&& dirs, EResolveFailPolicy failPolicy, bool allowDir = true);

    TFileView AddSrcToSymbols(TStringBuf src);

private:
    template<typename TIncl>
    void ResolveIncludes(TFileView src, const TVector<TIncl>& includes, TVector<TResolveFile>& result, TLangId langId = BY_SRC);

    void DumpResolveResult(TStringBuf src,
                           const TInclude& include,
                           const TResolveFile& targetView,
                           EResolveKind kind);
};

constexpr TModuleResolver::EResolveFailPolicy operator| (TModuleResolver::EResolveFailPolicy l, TModuleResolver::EResolveFailPolicy r) noexcept {
    using TUnderlying = std::underlying_type_t<TModuleResolver::EResolveFailPolicy>;
    return static_cast<TModuleResolver::EResolveFailPolicy>(
        static_cast<TUnderlying>(l) | static_cast<TUnderlying>(r)
    );
}

constexpr bool operator& (TModuleResolver::EResolveFailPolicy l, TModuleResolver::EResolveFailPolicy r) noexcept {
    using TUnderlying = std::underlying_type_t<TModuleResolver::EResolveFailPolicy>;
    return (static_cast<TUnderlying>(l) & static_cast<TUnderlying>(r)) != 0;
}

template<class TIncl>
void TModuleResolver::ResolveAsUnset(const TVector<TIncl>& includes, TVector<TResolveFile>& resolved) {
    resolved.reserve(resolved.size() + includes.size());
    for (const auto& include : includes) {
        TStringBuf name;
        if constexpr(std::is_same<TIncl, TInclude>()) {
            name = include.Path;
        } else {
            name = include;
        }
        if (NPath::IsTypedPath(name)) {
            resolved.emplace_back(MakeUnresolved(name)); // use TStringBuf directly
        } else {
            // use constructed TString with $U/
            resolved.emplace_back(MakeUnresolved(NPath::ConstructPath(name, NPath::Unset)));
        }
    }
};

/// Only for output TResolveFile to stream by use TModuleResolver
class TResolveFileOut {
public:
    TResolveFileOut(const TModuleResolver& moduleResolver, const TResolveFile resolveFile)
        : ModuleResolver_(moduleResolver)
        , ResolveFile_(resolveFile)
    {}

    ///Output TResolveFile to stream
    IOutputStream& Out(IOutputStream& os) const {
        auto [linkPrefix, targetPath] = ModuleResolver_.GetBufPair(ResolveFile_);
        if (!linkPrefix.empty()) {
            os << linkPrefix;
        }
        os << targetPath;
        return os;
    }

    /// Required for tests
    bool operator==(const TResolveFileOut& other) {
        return ResolveFile_ == other.ResolveFile_;
    }
private:
    const TModuleResolver& ModuleResolver_;
    const TResolveFile ResolveFile_;
};

/// Output TResolveFile to stream
static inline IOutputStream& operator<<(IOutputStream& os, const TResolveFileOut& resolveFileOut) {
    return resolveFileOut.Out(os);
}
