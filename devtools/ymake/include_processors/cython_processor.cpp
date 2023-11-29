#include "cython_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/module_wrapper.h>

#include <util/folder/path.h>
#include <util/generic/set.h>

namespace {
    constexpr auto CYTHON_CIMPORT = "cython"sv;

    //TODO(kikht): maybe replace with CYTHON_OUTPUT_INCLUDES from ymake.core.conf
    const TVector<TStringBuf> PREDEFINED_INDUCED = {
        "contrib/libs/python/Include/Python.h",
    };

    const TSet<TString> INNER_PARTS = {"libc", "libcpp", "cpython"};

    TStringBuf GetFirstPathPart(TStringBuf path) {
        if (path.empty()) {
            return path;
        }
        if (NPath::IsTypedPath(path)) {
            path = NPath::CutType(path);
        }
        TStringBuf first = path, _;
        path.TrySplit('/', first, _);
        return first;
    }

    bool IsNotResolved(const TVector<TResolveFile>& resolved) {
        return resolved.empty() || resolved[0].Root() == NPath::Unset;
    }

    bool IsResolved(const  TVector<TResolveFile>& resolved) {
        return !IsNotResolved(resolved);
    }
}

TCythonIncludeProcessor::TCythonIncludeProcessor(TSymbols& symbols)
    : TIncludeProcessorBase()
{
    Rule.Actions.clear();
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "pyx"}, TIndDepsRule::EAction::Use));
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "cpp"}, TIndDepsRule::EAction::Pass));
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "h+cpp"}, TIndDepsRule::EAction::Pass));
    Rule.PassInducedIncludesThroughFiles = true;


    LanguageC = NLanguages::GetLanguageId("c");
    LanguageCython = NLanguages::GetLanguageId("cython");
    Y_ABORT_UNLESS(LanguageC != NLanguages::BAD_LANGUAGE && LanguageCython != NLanguages::BAD_LANGUAGE);
}

void TCythonIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                              TModuleWrapper& module,
                                              TFileView incFileName,
                                              const TVector<TCythonDep>& includes) const {
    TVector<TString> preparedInduced;
    TVector<TString> preparedIncludes;

    TStringBuf incFileNameStr = incFileName.GetTargetStr();
    for (const auto& inclDep : includes) {
        if (inclDep.Path.Empty()) {
            continue;
        }
        switch (inclDep.Kind) {
            case TCythonDep::EKind::Cdef:
                preparedInduced.push_back(inclDep.Path);
                break;
            case TCythonDep::EKind::Include:
                preparedIncludes.push_back(inclDep.Path);
                break;
            case TCythonDep::EKind::CimportSimple:
                {
                    auto path = inclDep.Path;
                    if (GetFirstPathPart(path) == CYTHON_CIMPORT) {
                        break;
                    }
                    TVector<TResolveFile> resolved;
                    TString pxdPath = TString::Join(path, ".pxd");
                    module.ResolveSingleInclude(incFileName, pxdPath, resolved);
                    if (IsNotResolved(resolved)) {
                        auto&& savePxdPath = std::exchange(pxdPath, TString::Join(path, "/__init__.pxd"));
                        resolved.clear();
                        module.ResolveSingleInclude(incFileName, pxdPath, resolved);
                        if (IsNotResolved(resolved)) {
                            pxdPath = std::move(savePxdPath);
                        }
                    }
                    preparedIncludes.push_back(std::move(pxdPath));
                }
                break;
            case TCythonDep::EKind::CimportFrom:
                {
                    TStringBuf suffix = inclDep.Path;
                    TString searchPath = "";
                    if (suffix.SkipPrefix("."sv)) {
                        while (suffix.SkipPrefix("."sv)) {
                            searchPath += "../";
                        }
                    }
                    auto index = searchPath.size();
                    searchPath += suffix;
                    SubstGlobal(searchPath, ".", "/", index);
                    if (GetFirstPathPart(searchPath) == CYTHON_CIMPORT) {
                        break;
                    }

                    TVector<TResolveFile> resolved;
                    TString pxdPath = searchPath.empty() ? "__init__.pxd": TString::Join(searchPath, "/__init__.pxd");
                    module.ResolveSingleInclude(incFileName, pxdPath, resolved);
                    if (IsResolved(resolved)) {
                        preparedIncludes.push_back(std::move(pxdPath));
                    }
                    bool needCheckLists = true;
                    if (!searchPath.empty()) {
                        resolved.clear();
                        pxdPath = TString::Join(searchPath, ".pxd");
                        module.ResolveSingleInclude(incFileName, pxdPath, resolved);
                        if (IsResolved(resolved)) {
                            preparedIncludes.push_back(std::move(pxdPath));
                            needCheckLists = false;
                        }
                    }
                    if (needCheckLists) {
                        bool found = false;
                        for (const auto& cimport : inclDep.List) {
                            if (cimport == "*") {
                                continue;
                            }
                            TString searchPathCimport = searchPath.empty() ? cimport : TString::Join(searchPath, "/", cimport);
                            TString pxdCimport = TString::Join(searchPathCimport, "/__init__.pxd");
                            resolved.clear();
                            module.ResolveSingleInclude(incFileName, pxdCimport, resolved);
                            if (IsNotResolved(resolved)) {
                                pxdCimport = TString::Join(searchPathCimport, ".pxd");
                                resolved.clear();
                                module.ResolveSingleInclude(incFileName, pxdCimport, resolved);
                                if (IsNotResolved(resolved)) {
                                    continue;
                                }
                            }
                            preparedIncludes.push_back(pxdCimport);
                            found = true;
                        }
                        if (!found) {
                            resolved.clear();
                            TString pxdSearchPath = TString::Join(searchPath, "/__init__.pxd");
                            module.ResolveSingleInclude(incFileName, pxdSearchPath, resolved);
                            if (IsNotResolved(resolved)) {
                                pxdSearchPath = std::move(pxdPath);
                            }
                            preparedIncludes.push_back(pxdSearchPath);
                        }
                    }
                }
                break;
        }
    }

    TVector<TResolveFile> resolvedIncludes;
    module.ResolveLocalIncludes(incFileName, preparedIncludes, resolvedIncludes, LanguageId);

    if (incFileNameStr.EndsWith(".pyx")) {
        TString pxdPath = TString{incFileName.NoExtension()} + ".pxd";
        TFsPath fullPxdPath(module.ResolveToAbsPath(pxdPath));
        if (fullPxdPath.Exists()) {
            resolvedIncludes.emplace_back(module.AssumeResolved(pxdPath));
        }
    }

    if (!resolvedIncludes.empty()) {
        AddIncludesToNode(node, resolvedIncludes, module);
    }

    TVector<TResolveFile> resolvedInduced(Reserve(preparedInduced.size() + PREDEFINED_INDUCED.size()));
    module.ResolveAsUnset(preparedInduced, resolvedInduced);
    module.ResolveAsUnset(PREDEFINED_INDUCED, resolvedInduced);
    node.AddParsedIncls("cpp", resolvedInduced);
}

void TCythonIncludeProcessor::ProcessOutputIncludes(TAddDepAdaptor& node,
                                                    TModuleWrapper& module,
                                                    TFileView incFileName,
                                                    const TVector<TString>& includes) const {
    TVector<TCythonDep> typedIncludes(Reserve(includes.size() * 2));
    for (const auto& incl : includes) {
        TLangId lang = NLanguages::GetLanguageIdByExt(NPath::Extension(incl));
        bool langUnknown = (lang != LanguageCython && lang != LanguageC);

        if (lang == LanguageC || langUnknown) {
            typedIncludes.emplace_back(incl, TCythonDep::EKind::Cdef);
        }

        if (lang == LanguageCython || langUnknown) {
            typedIncludes.emplace_back(incl, TCythonDep::EKind::Include);
        }
    }
    ProcessIncludes(node, module, incFileName, typedIncludes);
}
