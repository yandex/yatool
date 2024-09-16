#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/iter.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/folder/path.h>
#include <util/generic/hash_set.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

struct TRestoreContext;

class TFoldersTree {
public:
    void Add(TStringBuf path);

    void Add(TFileView path) {
        Add(path.GetTargetStr());
    }

    bool Empty() const noexcept {
        return Map.empty();
    }

    bool EndOfPath() const noexcept {
        return IsEndOfPath;
    }

    bool ExistsProject() const noexcept {
        return Project != nullptr;
    }

    void SetProject(const TString& target) {
        Project = &target;
    }

    const TString* GetProject() const noexcept {
        return Project;
    }

    const THashMap<TStringBuf, TFoldersTree>& Data() const noexcept {
        return Map;
    }

    TFoldersTree* At(TStringBuf key) {
        return &Map[key];
    }

    const TFoldersTree* At(TStringBuf key) const {
        return Map.FindPtr(key);
    }

    bool ExistsParentPathOf(TStringBuf path) const;

    TVector<TString> GetPaths() const;

private:
    THashMap<TStringBuf, TFoldersTree> Map;
    const TString* Project = nullptr;
    bool IsEndOfPath = false;
};

class TIsolatedProjects {
public:
    // load isolated projects list
    void Load(const TFsPath& sourceRoot, const TVector<TStringBuf>& lists, MD5& confHash);

    // check DATA|DEPENDS reference to path from makefile not cause usage isolated projects data from another project (use FoldersTree_)
    void CheckStatementPath(TStringBuf statement, TStringBuf makefile, TStringBuf path) const;

    const TVector<TString>& Projects() const noexcept {
        return Projects_;
    }
    bool Empty() const noexcept {
        return Projects_.empty();
    }

    void CheckAll(const TRestoreContext& restoreContext, const TVector<TTarget>& startTargets, const TRestoreContext& recurseRestoreContext, const TVector<TTarget>& recurseStartTargets) const;

    // use ymake dependecy graph + own info(Makefile*Refs_) for check existing dependency to isolated projects from another projects
    void ReportDeps(const TRestoreContext& restoreContext, const TVector<TTarget>& startTargets) const;
    void ReportRecurseDeps(const TRestoreContext& recurseRestoreContext, const TVector<TTarget>& recurseStartTargets) const;

    virtual ~TIsolatedProjects() = default;
protected:
    // error handlers for parsing Isolated projects list files (declared virtual for override in unit tests)
    virtual void OnAbsolutePath(TStringBuf path, TStringBuf file) const;
    virtual void OnInvalidPath(TStringBuf path, TStringBuf file) const;
    virtual void OnEmptyPath(TStringBuf path, TStringBuf file) const;
    virtual void OnIncludedProjectPath(TStringBuf includedPath, const THashSet<TStringBuf>& filesWithIncludedPath, TStringBuf path, const THashSet<TStringBuf>& files) const;

    // error handler for detecting usage isolated project from another project
    virtual void OnDepToIsolatedProject(const TString& project, TStringBuf dependFrom, const TString& dependency) const;

    using TProjectPathToSources = TMap<TString, THashSet<TStringBuf>>;
    void LoadFromString(TProjectPathToSources&, TStringBuf content, TStringBuf filename);
    void LoadProjectsIgnoreIncluded(const TProjectPathToSources&);

    // walk by folders tree (projects) and call error handler if makefile not in the same project folder
    void CheckMakefilePlacedInProjects(TStringBuf statement, TStringBuf makefile, const TFoldersTree& folder, TStringBuf dataFile) const;
    void CheckMakefilePlacedInProject(TStringBuf statement, TStringBuf makefile, const TString& project, TStringBuf dataFile) const;

private:
    TVector<TString> Projects_;  // (sorted)
    TFoldersTree FoldersTree_;  // projects, organized in folders tree (data keep pointers to Projects_, so MUST BE rebuild after Projects_ modifications)
};
