#include "module_resolver.h"

#include "parser_manager.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/common/restore_guard.h>
#include <devtools/ymake/include_processors/include.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag/manager.h>

#include <library/cpp/json/json_value.h>
#include <library/cpp/json/json_writer.h>

#include <util/generic/scope.h>

TStringBuf CheckUnresolvedName(TStringBuf name) {
    Y_ASSERT(!NPath::IsTypedPathEx(name) || !NPath::IsLink(name) && !NPath::IsType(name, NPath::Unset));
    return name;
}

template <typename TDirList>
void TModuleResolver::ReportPathAsUnset(TStringBuf name, TDirList&& dirs) const {
    Y_ASSERT(!NPath::IsLink(name));
    NEvent::TInvalidFile event;
    event.SetFile(TString{name});

    // This will be nicer with C++17 due to deduction of 2nd template argument from c'tor argument's type
    auto fillFn = [&event](TFileView dir) { event.AddDirs(TString{dir.GetTargetStr()}); };
    TIterTupleRunner<TFileView, decltype(fillFn)> filler(fillFn);

    filler.Run(dirs);
    TRACE(P, event);
}

void TModuleResolver::ReportSourcePathAsUnset(TStringBuf name) const {
    ReportPathAsUnset(name, MakeResolvePlan(MakeIterPair(Module.SrcDirs)));
}

bool TModuleResolver::FormatBuildPath(TVarStrEx& var, TFileView srcDir, TFileView buildDir) {
    Y_ASSERT(!NPath::IsTypedPath(var.Name) || !NPath::IsType(var.Name, NPath::Link));

    if (!var.DirAllowed && NPath::IsExplicitDirectory(var.Name)) {
        // Directories are not allowed as output
        return false;
    }

    if (var.ResolveToModuleBinDirLocalized) {
        var.Name = ResolveToModuleBinDirLocalized(var.Name);
    } else if (var.IsPathResolved || ResolveAsKnownWithoutCheck(var)) {
        if (!NPath::IsType(var.Name, NPath::Build)) {
            var.Name = NPath::SetType(var.Name, NPath::Build);
        }
    } else {
        auto& fileConf = Ctx.Graph.Names().FileConf;
        if (var.NoRel) {
            var.Name = NPath::Join(fileConf.ReplaceRoot(srcDir, NPath::Build).GetTargetStr(), var.Name);
        } else if (var.ResolveToBinDir) {
            var.Name = NPath::Join(fileConf.ReplaceRoot(Module.GetDir(), NPath::Build).GetTargetStr(), var.Name);
        } else {
            var.Name = NPath::Join(buildDir.GetTargetStr(), var.Name);
        }
    }

    if (NPath::HasWinSlashes(var.Name)) {
        var.Name = NPath::NormalizeSlashes(var.Name);
    } else if (NPath::NeedFix(var.Name)) {
        var.Name = NPath::Reconstruct(var.Name);
    }
    var.IsPathResolved = true;
    var.ElemId = 0; // Name may be changed, ElemId may be invalid
    return true;
}

TFileView TModuleResolver::AddSrcToSymbols(TStringBuf src) {
    return Ctx.Graph.Names().FileConf.GetStoredName(src);
}

TResolveFile TModuleResolver::ResolveAsOutput(TStringBuf name) {
    TStringBuf pathBuf = name;
    if (!NPath::IsTypedPath(name) || !NPath::IsType(name, NPath::Build)) {
        pathBuf = _TempStr = NPath::ConstructPath(name, NPath::Build);
    }
    TResolveFile result;
    if (Resolver().ResolveAsKnownWithCheck(pathBuf, Module.GetDir(), result) == RESOLVE_SUCCESS) {
        return result;
    }
    return MakeUnresolved(name);
}

TString TModuleResolver::ResolveToModuleBinDirLocalized(TStringBuf path) {
    TStringBuf yPathBuf;
    TFileView yFileView;
    if (!Resolver().ResolveAsKnownWithoutCheck(path, Module.GetDir(), _TempStr)) {
        yPathBuf = path;
    } else {
        yPathBuf = _TempStr;
    }
    if (NPath::IsTypedPathEx(yPathBuf)) {
        yPathBuf = NPath::CutType(yPathBuf);
    }
    Y_ASSERT(!yPathBuf.empty() && yPathBuf.front() != '$' && yPathBuf.front() != '/');
    const TStringBuf modPath = Module.GetDir().CutType();
    const TStringBuf localSubDir = "_l_";
    return BuildPath(NPath::Join(modPath, localSubDir, yPathBuf));
}

template <typename TDirList>
TResolveFile TModuleResolver::ResolvePath(TStringBuf name, TFileView curDir, TDirList&& dirs, TModuleResolver::EResolveFailPolicy failPolicy, bool allowDir) {
    auto& fileConf = Ctx.Graph.Names().FileConf;
    TFileView curBldDir = curDir.IsValid() ? fileConf.ReplaceRoot(curDir, NPath::Build) : TFileView();

    auto resolvePlan = MakeResolvePlan(curDir, dirs, fileConf.SrcDir(), curBldDir, fileConf.BldDir());

    auto& resolver = Resolver();
    auto& resolverOptions = resolver.MutableOptionsWithClear();
    TRestoreGuard restoreOptions(resolverOptions);
    resolverOptions.AllowDir = allowDir;
    EResolveStatus result = resolver.ResolveName(name, curDir, resolvePlan);

    TResolveFile resolveFile;
    if (result == RESOLVE_SUCCESS) {
        // Uncomment this for multiple resolving variants diagnostics
        // if (Resolver().GetVariants().size() > 1) {
        //     ReportAmbigousSource();
        // }
        resolveFile = resolver.Result();
        YDIAG(PATH) << "src: resolved " << name << " as " << TResolveFileOut(*this, resolveFile) << Endl;
    } else if (failPolicy & LastTry) {
        if (result == RESOLVE_ERROR_ABSOLUTE) {
            TScopedContext context(Module.GetName());
            YConfErr(BadFile) << "source file name is outside the build tree: " << name << Endl;
            ReportSourcePathAsUnset(name);
            resolveFile = Resolver().MakeUnresolved(name, resolver.NameContext());
        } else {
            Y_ASSERT((result == RESOLVE_FAILURE) || (result == RESOLVE_FAILURE_MISSING));
            // This may be converted and fixed.
            // If name belongs to known root we will record it here with root embedded.
            // This is needed to avoid attributing it to other root on retry
            TStringBuf unresolvedName = CheckUnresolvedName(resolver.Name());
            if (!(failPolicy & Silent)) {
                TScopedContext context(Module.GetName());
                YConfErr(BadFile) << "cannot find source file: " << unresolvedName << "\n";
                ReportPathAsUnset(unresolvedName, dirs);
            }
            resolveFile = Resolver().MakeUnresolved(unresolvedName, resolver.NameContext());
        }
    }
    return resolveFile;
}

template <typename TDirList>
bool TModuleResolver::ResolvePath(TVarStrEx& src, TFileView curDir, TDirList&& dirs, TModuleResolver::EResolveFailPolicy failPolicy, bool allowDir) {
    if (src.IsMacro) {
        if (!src.ElemId) {
            src.ElemId = Ctx.Graph.Names().CommandConf.GetStoredName(src.Name).GetElemId();
        };
        return true;
    }
    if (src.IsPathResolved) {
        NPath::ValidateEx(src.Name);
        if (!src.ElemId) {
            src.ElemId = Ctx.Graph.Names().FileConf.GetStoredName(src.Name).GetElemId();
        };
        return true;
    }
    YDIAG(PATH) << "resolve path: " << src.Name << Endl;
    auto resolveFile = ResolvePath(src.Name, curDir, dirs, failPolicy, allowDir);
    bool result = false;
    if (!resolveFile.Empty()) {
        src.IsPathResolved = true;
        src.Name = GetStr(resolveFile);
        src.ElemId = resolveFile.GetElemId();
        auto pathWithoutContext = NPath::ResolveLink(src.Name);
        auto linkType = NPath::GetType(pathWithoutContext);
        if (linkType != NPath::Unset) {
            src.NotFound = false;
            src.IsOutputFile = linkType == NPath::Build;
            Y_ASSERT(resolveFile.GetElemId() == Resolver().Result().GetElemId()); // here resolveFile must be copy of Resolver().Result()
            src.IsDir = Resolver().IsDir();
            result = true;
        }
    }
    src.NotFound = !result;
    return result;
}

TResolveFile TModuleResolver::ResolveSourcePath(TStringBuf name, TFileView curDir, TModuleResolver::EResolveFailPolicy failPolicy, bool allowDir) {
    return ResolvePath(name, curDir, MakeResolvePlan(MakeIterPair(Module.SrcDirs)), failPolicy, allowDir);
}

// ATTN: this function may not alter file's base name or extension
bool TModuleResolver::ResolveSourcePath(TVarStrEx& src, TFileView curDir, TModuleResolver::EResolveFailPolicy failPolicy, bool allowDir) {
    return ResolvePath(src, curDir, MakeResolvePlan(MakeIterPair(Module.SrcDirs)), failPolicy, allowDir);
}

// ATTN: this function may not alter file's base name or extension
bool TModuleResolver::ResolveInducedDepPath(TVarStrEx& src, TFileView curDir, TModuleResolver::EResolveFailPolicy failPolicy) {
    const auto incDirs = Module.IncDirs.Get(NLanguages::GetLanguageIdByExt(NPath::Extension(src.Name)));
    return ResolvePath(src, curDir, MakeResolvePlan(MakeIterPair(Module.SrcDirs), MakeIterPair(incDirs)), failPolicy | Silent);
}

void TModuleResolver::DumpResolveResult(TStringBuf src,
                                       const TInclude& include,
                                       const TResolveFile& targetView,
                                       EResolveKind kind) {
    if (Y_LIKELY(!Conf.DumpIncludeTargets)) {
        return;
    }

    auto [linkPrefix, targetPath] = GetBufPair(targetView);

    if (kind == Local && NPath::CutType(targetPath) == include.Path) {
        kind = ArcadiaRoot;
    }

    NJson::TJsonWriter writer(&Cout, false, false, true);
    writer.OpenMap();
    writer.Write("src", src);
    writer.Write("include", include.Path);
    writer.Write("include_kind", ToString(include.Kind));
    if (linkPrefix.empty()) {
        writer.Write("target", targetPath);
    } else {
        writer.Write("target", TStringBuilder() << linkPrefix << targetPath);
    }
    writer.Write("resolve_kind", ToString(kind));
    writer.CloseMap();
    writer.Flush();
    Cout << Endl;
}

/**
Note of GCC include search rules:
  1. #include "file" (TInclude::EKind::Local) only: current src's directory
  2. #include "file" only: -iquote directories (we don't use them)
  3. both #include "file" and #include <file> (TInclude::EKind::System): -I directories
  4. platform's system dirs
Our rules for include resolving:
  1. Source dir (only for local includes)
  2. Arcadia root dir. (build + src)
  3. Module dirs (build + src), module's SRCDIR/ADDINCL & peerdir'ed modules GLOBAL SRCDIR/ADDINCL
  4. System includes (SYSINCL)
If we can't resolve include in 1. and 2. we resolve both by 3. and 4. and if both
methods resolve into something we check that result of 3 is contained in sysincl
result and produce warning if it is not.
*/
void TModuleResolver::ResolveSingleInclude(TFileView src, const TInclude& include, TVector<TResolveFile>& result, TLangId langId) {
    auto& fileConf = Ctx.Graph.Names().FileConf;

    YDIAG(VV) << "include: " << include << " from " << src << Endl;
    if (Module.GetAttrs().DontResolveIncludes) {
        result.push_back(MakeUnresolved(include.Path));
        return;
    }

    auto& resolver = Resolver();
    auto& resolverOptions = resolver.MutableOptionsWithClear();
    TRestoreGuard restoreOptions(resolverOptions);
    resolverOptions.AllowDir = false;
    resolverOptions.AllowAbsRoot = false;
    src.GetStr(_SrcName);
    TFileView srcDir = fileConf.Parent(src);
    srcDir = fileConf.ResolveLink(srcDir);

    bool isKnownRoot = NPath::IsKnownRoot(include.Path);

    if (!isKnownRoot && langId == BY_SRC) {
        langId = NLanguages::GetLanguageIdByExt(src.Extension());
        if (langId == TModuleIncDirs::BAD_LANG) {
            auto parserId = Ctx.ParsersCache.GetParserId(src.GetElemId(), NLanguages::ParsersCount());
            if (parserId != TParsersCache::BAD_PARSER_ID) {
                langId = NLanguages::GetLanguageIdByParserId(parserId);
            } else {
                langId = NLanguages::GetLanguageId("c");
            }
        }
    }

    // These are values for ResolvePlanKey
    // Those should be unique for Module and particular reslving invocation
    const int RP_KEY_ONLY_ROOTS = 0;

    // This one always has bits in upper part and only lowest bit in lower part
    auto rpKeySrcDirFn = [](TFileView srcDir, bool withRoots) -> ui64 {
        return ((ui64)srcDir.GetElemId() << 32) + (ui64)withRoots;
    };

    // This one has lower part >=2 so will never clash with 0 or previous Fn.
    // Here we rely on the fact that incDirs are append-only during module construction.
    // However module may be restored from cache and then rebuilt, so we add this info
    // to avoid InDirs sizes collision.
    auto rpKeyIncDirsFn = [](size_t numDirs, TLangId langId, bool modLoaded) -> ui64 {
        return ((ui64)numDirs << 32) + (modLoaded << 31) + (ui64)langId + 2;
    };

    auto ResolveWithCache = [&](size_t resolvePlanKey, auto resolvePlan) -> const TResolveCacheValue* {
        Ctx.ResolveStats.Inc(NStats::EResolveStats::IncludesAttempted);
        if (isKnownRoot) {
            Ctx.ResolveStats.Inc(NStats::EResolveStats::ResolveAsKnownTotal);
        }
        TResolveCacheKey key {include.Path, resolvePlanKey};
        auto it = Ctx.ResolveCache->Get(key);
        if (!Ctx.ResolveCache->Found(it)) {
            EResolveStatus status = resolver.ResolveName(include.Path, srcDir, resolvePlan);
            TResolveCacheValue cacheValue{
                status,
                status == RESOLVE_SUCCESS ? resolver.Result() : TResolveFile(),
                status == RESOLVE_SUCCESS ? "" : CheckUnresolvedName(resolver.Name())
            };
            it = Ctx.ResolveCache->Put(std::move(key), std::move(cacheValue));
            Y_ASSERT(Ctx.ResolveCache->Found(it));
        } else {
            Ctx.ResolveStats.Inc(NStats::EResolveStats::IncludesFromCache);
            if (isKnownRoot) {
                Ctx.ResolveStats.Inc(NStats::EResolveStats::ResolveAsKnownFromCache);
            }
        }
        Y_ASSERT((!it->second.View.Empty()) || (!it->second.UnresolvedName.Empty()));
        return &it->second;
    };

    const TResolveCacheValue* localResult = nullptr;
    if (!isKnownRoot && include.Kind == TInclude::EKind::Local) {
        if (Conf.IsRequiredBuildAndSrcRoots(NLanguages::GetLanguageName(langId))) {
            localResult = ResolveWithCache(rpKeySrcDirFn(srcDir, true), MakeResolvePlan(srcDir, fileConf.BldDir(), fileConf.SrcDir()));
        } else {
            localResult = ResolveWithCache(rpKeySrcDirFn(srcDir, false), MakeResolvePlan(srcDir));
        }
    } else if (isKnownRoot || include.Kind == TInclude::EKind::System) {
        localResult = ResolveWithCache(RP_KEY_ONLY_ROOTS, MakeResolvePlan(fileConf.BldDir(), fileConf.SrcDir()));
    } else {
        Y_ASSERT(include.Kind == TInclude::EKind::Macro);
        TSysinclResolver::TResult preResolveResult = Conf.Sysincl.Resolve(src, include.Path);
        if (preResolveResult) {
            // We need to preserve Conf.Sysincl.Result here for reentrant call in the loop.
            // However we are the only consumer of this info, so we may take it.
            const auto res = std::move(*preResolveResult);
            for (TStringBuf sysinclPath : res) {
                TInclude alias{TInclude::EKind::System, sysinclPath};
                ResolveSingleInclude(src, alias, result, langId);
            }
        } else if (Conf.ShouldForceResolveMacroIncls()) {
            YConfErr(BadIncl) << "Can't resolve macro target " << include.Path << " from " << src << Endl;
        }
        return;
    }

    if (localResult->IsSuccess()) {
        YDIAG(VV) << " resolved locally to " << TResolveFileOut(*this, localResult->View) << Endl;
        DumpResolveResult(_SrcName, include, localResult->View, Local);
        result.emplace_back(localResult->View);
        return;
    } else if (localResult->Status == RESOLVE_ERROR_ABSOLUTE) {
        YConfWarn(BadIncl) << "absolute include target " << include.Path
                           << " from " << src << Endl;
        return;
    } else if (localResult->Status == RESOLVE_FAILURE_MISSING) {
        result.push_back(MakeUnresolved(localResult->UnresolvedName));
        return;
    }

    const auto incDirs = Module.IncDirs.Get(langId);
    size_t numDirs = incDirs.Total();
    resolverOptions.ResolveAsKnown = false; // This should happen in first call above
    const TResolveCacheValue* addinclResult = numDirs > 0 ?
                                   ResolveWithCache(rpKeyIncDirsFn(numDirs, langId, Module.IsLoaded()), MakeResolvePlan(MakeIterPair(incDirs))) :
                                   localResult;
    resolverOptions.ResolveAsKnown = true;

    if (addinclResult->IsSuccess()) {
        YDIAG(VV) << " resolved via addincl to " << TResolveFileOut(*this, addinclResult->View) << Endl;
        DumpResolveResult(_SrcName, include, addinclResult->View, Addincl);
    }
    Y_ASSERT(addinclResult->Status != RESOLVE_ERROR_ABSOLUTE);

    TSysinclResolver::TResult sysinclResult = Conf.Sysincl.Resolve(src, include.Path);
    Y_ASSERT(addinclResult->IsSuccess() ? !addinclResult->View.Empty() : !addinclResult->UnresolvedName.Empty());
    if (!sysinclResult) {
        if (addinclResult->IsSuccess()) {
            result.emplace_back(addinclResult->View);
        } else {
            result.push_back(MakeUnresolved(addinclResult->UnresolvedName));
        }
        return;
    }

    bool addinclInsideSysincl = false;
    TResolveFile resolvedInclude;
    for (const TString& sysinclPath : *sysinclResult) {
        auto resolved = ResolveAsKnownWithCheck(sysinclPath, resolvedInclude);
        if (resolved == RESOLVE_SUCCESS) {
            if ((addinclResult->IsSuccess()) &&
                (resolvedInclude.GetElemId() == addinclResult->View.GetElemId())) {
                addinclInsideSysincl = true;
            }
            YDIAG(VV) << " resolved via sysincl to " << TResolveFileOut(*this, resolvedInclude) << Endl;
            DumpResolveResult(_SrcName, include, resolvedInclude, Sysincl);
            result.emplace_back(resolvedInclude);
        } else {
            TStringBuf resolvedIncludeBuf = resolver.Name();
            ReportPathAsUnset(resolvedIncludeBuf, MakeResolvePlan());
            result.emplace_back(MakeUnresolved(resolvedIncludeBuf, resolver.NameContext()));
            YConfErr(Misconfiguration) << "can't resolve sysincl target: " << sysinclPath << Endl;
        }
    }

    if (addinclResult->IsSuccess() && !addinclInsideSysincl) {
        result.emplace_back(addinclResult->View);
        YConfErr(BadIncl) << "sysincl/addincl mismatch for include " << include.Path
                          << " from " << src << " addincl: " << TResolveFileOut(*this, addinclResult->View)
                          << " sysincls: " << JoinStrings(*sysinclResult, " ") << Endl;
    }
}

template<class TIncl>
void TModuleResolver::ResolveIncludes(TFileView src, const TVector<TIncl>& includes, TVector<TResolveFile>& result, TLangId langId) {
    if (includes.empty()) {
        return;
    }
    result.clear();
    result.reserve(includes.size());
    YDIAG(VV) << "source scan: " << src << " " << includes.size() << " includes" << Endl;
    for (const auto& include : includes) {
        if constexpr(std::is_same<TIncl, TInclude>()) {
            Y_ASSERT(!include.Path.empty());
            ResolveSingleInclude(src, include, result, langId);
        } else {
            TInclude includeObj(include);
            Y_ASSERT(!includeObj.Path.empty());
            ResolveSingleInclude(src, includeObj, result, langId);
        }
    }
}

void TModuleResolver::ResolveIncludes(TFileView src, const TVector<TInclude>& includes, TVector<TResolveFile>& result, TLangId langId) {
    ResolveIncludes<TInclude>(src, includes, result, langId);
}

void TModuleResolver::ResolveLocalIncludes(TFileView src, const TVector<TString>& includes, TVector<TResolveFile>& result, TLangId langId) {
    ResolveIncludes<TString>(src, includes, result, langId);
}

void TModuleResolver::ResolveLocalIncludes(TFileView src, const TVector<TStringBuf>& includes, TVector<TResolveFile>& result, TLangId langId) {
    ResolveIncludes<TStringBuf>(src, includes, result, langId);
}
