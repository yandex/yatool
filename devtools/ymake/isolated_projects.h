#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/iter.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/folder/path.h>
#include <util/generic/hash_set.h>
#include <util/generic/map.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <functional>
#include <optional>
#include <variant>

struct TRestoreContext;
class TModules;
class TModule;

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
    enum class EDepStatement {
        Depends,
    };

    // All messages have the Error severity and the -WIslPrjs kind.
    // Apply() requires the original graph with stable node IDs and symbol tables.
    // Apply in the analysis phase's diagnostic context.
    struct TCheckResult {
        struct TDependencyPath {
            TFileView From;
            TFileView To; // DATA targets may have no graph node.
            TVector<TNodeId> Nodes; // Intermediate graph nodes, in dependency order.
        };

        struct TStatementDependency {
            EDepStatement Statement;
            TFileView Path;
        };

        class TDiagnostic {
        public:
            TDiagnostic(TFileView owner, TString project, TDependencyPath dependency);
            TDiagnostic(TFileView owner, TString project, TStatementDependency dependency);

            TFileView Owner() const {
                return Owner_;
            }

            const TString& Project() const {
                return Project_;
            }

            const std::variant<TDependencyPath, TStatementDependency>& Dependency() const {
                return Dependency_;
            }

            void Report(const TDepGraph& graph) const;

        private:
            // Statement diagnostics always have a valid owner. For graph paths,
            // an invalid view keeps the phase's ambient owner (no enclosing module).
            TFileView Owner_;
            TString Project_;
            std::variant<TDependencyPath, TStatementDependency> Dependency_;
        };

        struct TStatistics {
            size_t DepsCount = 0;
            size_t PathNodesCount = 0;
            TMap<size_t, size_t> PathLengths;
        };

        // Graph traversal finishes collecting erasures before producing messages.
        bool EraseAll = false;
        TVector<TFileElemId> OwnersToErase;
        TVector<TDiagnostic> Diagnostics;
        std::optional<TStatistics> Statistics;

        void Apply(const TDepGraph& graph) const;
    };

    // load isolated projects list
    void Load(const TFsPath& sourceRoot, const TVector<TStringBuf>& lists, MD5& confHash);

    // check DATA|DEPENDS reference to path from makefile not cause usage isolated projects data from another project (use FoldersTree_)
    void CheckStatementPath(TStringBuf statement, TFileView makefile, TStringBuf path) const;

    const TVector<TString>& Projects() const noexcept {
        return Projects_;
    }
    bool Empty() const noexcept {
        return Projects_.empty();
    }

    class TCheckInput {
    public:
        struct TStatement {
            TFileView Makefile;
            TFileView Path;
        };

        const TDepGraph& Graph() const {
            return Graph_;
        }

        const TModules& Modules() const {
            return Modules_;
        }

        bool ShouldEraseModuleErrors(const TModule& module) const;

    private:
        friend class TIsolatedProjects;
        TCheckInput(const TDepGraph& graph, const TModules& modules)
            : Graph_(graph)
            , Modules_(modules)
        {}

        const TDepGraph& Graph_;
        const TModules& Modules_;
        bool Enabled_ = false;
        bool EraseAll_ = false;
        TVector<TNodeId> StartTargets_;
        bool HashChanged_ = false;
        THashSet<TFileElemId> OwnersWithErrors_;
        TVector<TStatement> Statements_;
    };

    // Preparation runs on the main analysis flow. Graph topology, node IDs,
    // symbol tables and this project's list must remain alive and stable until Apply.
    // Modules must outlive computation; DATA paths must not change after graph construction.
    [[nodiscard]] TCheckInput PrepareCheck(const TRestoreContext& restoreContext, const TVector<TTarget>& startTargets, const TRestoreContext& recurseRestoreContext, const TVector<TTarget>& recurseStartTargets) const;
    // May materialize modules under the store lock; run with the configuration executor context.
    // Reads stable module fields and prepared error owners, never the live message cache.
    [[nodiscard]] TCheckResult CheckAll(const TCheckInput& input) const;

    // Called by graph validation after PrepareCheck has captured the warning flag.
    void CollectStatementPath(EDepStatement statement, TFileView makefile, TFileView path, TCheckResult& result) const;

    virtual ~TIsolatedProjects() = default;
protected:
    // error handlers for parsing Isolated projects list files (declared virtual for override in unit tests)
    virtual void OnAbsolutePath(TStringBuf path, TStringBuf file) const;
    virtual void OnInvalidPath(TStringBuf path, TStringBuf file) const;
    virtual void OnEmptyPath(TStringBuf path, TStringBuf file) const;
    virtual void OnIncludedProjectPath(TStringBuf includedPath, const THashSet<TStringBuf>& filesWithIncludedPath, TStringBuf path, const THashSet<TStringBuf>& files) const;

    // error handler for detecting usage isolated project from another project
    virtual void GenerateIsolatedProjectsError(const TString& project, const TFileView& dependencySource, const TString& dependencyDetails) const;

    using TProjectPathToSources = TMap<TString, THashSet<TStringBuf>>;
    void LoadFromString(TProjectPathToSources&, TStringBuf content, TStringBuf filename);
    void LoadProjectsIgnoreIncluded(const TProjectPathToSources&);

private:
    using TErrorHandler = std::function<void(const TString&, TFileView, TStringBuf, TStringBuf)>;

    void CollectDeps(const TCheckInput& input, TCheckResult& result) const;

    void CheckStatementPath(TStringBuf statement, TFileView makefile, TStringBuf path, const TErrorHandler& onError) const;

    // walk by folders tree (projects) and call error handler if makefile not in the same project folder
    void CheckMakefilePlacedInProjects(TStringBuf statement, TFileView makefile, const TFoldersTree& folder, TStringBuf dataFile, const TErrorHandler& onError) const;
    void CheckMakefilePlacedInProject(TStringBuf statement, TFileView makefile, const TString& project, TStringBuf dataFile, const TErrorHandler& onError) const;

private:
    TVector<TString> Projects_;  // (sorted)
    TFoldersTree FoldersTree_;  // projects, organized in folders tree (data keep pointers to Projects_, so MUST BE rebuild after Projects_ modifications)
};
