#include "isolated_projects.h"

#include "conf.h"
#include "prop_names.h"
#include "module_state.h"
#include "module_store.h"
#include "module_restorer.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/macro_string.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/folder/path.h>
#include <util/generic/deque.h>
#include <util/generic/stack.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/queue.h>
#include <util/stream/file.h>
#include <util/string/split.h>
#include <util/string/strip.h>

#include <algorithm>

using namespace NYMake;

namespace {
    /// visitor for collecting dependency between sources
    /// also keep anothed type nodes info for deps (path between 'From' & 'To')
    class TDepsCollectorVisitor: TDirectPeerdirsConstVisitor<> {
    public:
        using TBase = TDirectPeerdirsConstVisitor<>;
        using TState = typename TBase::TState;
        struct TSourceDep {  // dependency between sources
            using TPathId = ui32;
            static const TPathId EmptyPath = 0;

            ui32 From;  // elem id
            ui32 To;  // elem id
            ui32 ModuleId; // elem id of current module
            TPathId PathId{EmptyPath}; // index fist path node in TDepsCollectorVisitor::Paths (nodes on path from 'From' to 'To')
        };

        TDepsCollectorVisitor(const TRestoreContext& restoreContext, bool isIsolatedProjectsHashChanged)
            : RestoreContext_(restoreContext)
            , IsIsolatedProjectsHashChanged_(isIsolatedProjectsHashChanged)
        {
            Paths_.push_back(TNodeId::Invalid); // reserve place for EmptyPath id
        }

        bool Enter(TState& state) {
            if (!TBase::Enter(state)) {
                return false;
            }
            auto nodeRef = state.TopNode();
            auto nodeId = nodeRef.Id();
            auto nodeType = nodeRef->NodeType;
            if (nodeType == EMNT_File || nodeType == EMNT_Directory || nodeType == EMNT_MakeFile || IsModuleType(nodeType)) {
                SourcesNodesStack_.emplace_back(nodeId);
                if (IsModuleType(nodeType)) {
                    const auto* module = RestoreContext_.Modules.Get(nodeRef->ElemId);
                    if (RequireModuleRecheck(module)) {
                        YConfEraseByOwner(IslPrjs, module->GetId()); // clear all existing module isolated projects errors
                    }
                    if (module->DataPaths) {
                        for (const auto dirView : *module->DataPaths) {
                            AddDep(state, dirView.GetTargetId());
                        }
                    }
                }
            } else if (nodeType == EMNT_BuildCommand && state.Size() >= 2 && state.Parent()->CurDep().Value() == EDT_Property) {
                ui64 propId;
                TStringBuf propType, propValue;
                ParseCommandLikeProperty(TDepGraph::GetCmdName(nodeRef).GetStr(), propId, propType, propValue);
                if (propType == NProps::LATE_GLOB) {
                    LateGlob_.emplace_back(SourcesNodesStack_.size());
                }
            }
            return true;
        }

        void Leave(TState& state) {
            auto nodeId = state.TopNode().Id();
            if (SourcesNodesStack_.size() && SourcesNodesStack_.back() == nodeId) {
                if (LateGlob_.size() && LateGlob_.back() == SourcesNodesStack_.size()) {
                    LateGlob_.pop_back();
                }
                SourcesNodesStack_.pop_back();
            }
            TBase::Leave(state);
        }

        void Left(TState& state) {
            TBase::Left(state);
        }

        bool AcceptDep(TState& state) {
            const auto& graph = RestoreContext_.Graph;
            const auto& fileConf = graph.Names().FileConf;
            auto depRef = state.NextDep();
            auto toNodeRef = depRef.To();
            auto toNodeType = toNodeRef->NodeType;

            if (depRef.Value() == EDT_Property) {
                bool ignoreProperty = true;
                if (toNodeType == EMNT_BuildCommand) {
                    ui64 propId;
                    TStringBuf propType, propValue;
                    ParseCommandLikeProperty(TDepGraph::GetCmdName(toNodeRef).GetStr(), propId, propType, propValue);
                    if (propType == NProps::LATE_GLOB) {
                        ignoreProperty = false;  // sad story: (JAVA_SRCS, DOCS_BUILDER)
                    }
                } else if (LateGlob_.size() && toNodeType == EMNT_File) {
                    // sad story: (JAVA_SRCS, DOCS_BUILDER)
                    // as example LATE_GLOB(JAVA_SRCS) ref to java sources via Property:
                    //
                    // [0 of 0] Dep: Search, Type: Directory, Id: 5, Name: $S/advq/java/libs/advq-db-util
                    //  [1 of 2] Dep: Include, Type: MakeFile, Id: 6, Name: $L/MKF/$S/advq/java/libs/advq-db-util/ya.make
                    //   [3 of 3] Dep: Property, Type: BuildCommand, Id: 16, Name: 8:LATE_GLOB=src/main/java/**/*
                    //    [3 of 19] Dep: Property, Type: File, Id: 15, Name: $L/TEXT/$S/advq/java/libs/advq-db-util/src/main/java/ru/yandex/advq/db/util/Centroid.java
                    ignoreProperty = false;
                }
                if (ignoreProperty) {
                    return false;
                }
            }

            if (toNodeType == EMNT_File || toNodeType == EMNT_Directory || toNodeType == EMNT_MakeFile || IsModuleType(toNodeType)) {
                AddDep(state, fileConf.GetTargetId(toNodeRef->ElemId));
            }

            return TBase::AcceptDep(state);
        }

        void AddDep(TState& state, ui32 toElemId) {
            Y_ASSERT(toElemId);
            if (SourcesNodesStack_.empty()) {
                return; // From absent
            }
            const auto& graph = RestoreContext_.Graph;
            const auto& fileConf = graph.Names().FileConf;
            ui32 fromElemId = fileConf.GetTargetId(graph.Get(TNodeId{ToUnderlying(SourcesNodesStack_.back()) & 0x7fffffff})->ElemId);
            Y_ASSERT(fromElemId);
            const auto moduleIt = FindModule(state);
            Deps_.emplace_back(TSourceDep{fromElemId, toElemId, moduleIt != state.end() ? moduleIt->Node()->ElemId : 0}); // store dep here
            // store dependency path between sources
            size_t pathsSize = Paths_.size();
            for (auto it = state.Stack().rbegin(); it != state.Stack().rend(); ++it) {
                if (it->Node().Id() == SourcesNodesStack_.back()) {
                    break;
                }
                Paths_.emplace_back(it->Node().Id());
            }
            if (pathsSize != Paths_.size()) {
                Y_ASSERT(pathsSize <= LastPathNodeBit_);
                Deps_.back().PathId = static_cast<typename TSourceDep::TPathId>(pathsSize);
                Paths_.back() = TNodeId{ToUnderlying(Paths_.back()) | LastPathNodeBit_};
                PathsLengthStat_[Paths_.size() - pathsSize] += 1; // collect debug/info statistic for paths length
            }
        }

        void SortDepsByTo() {
            const auto& fileConf = RestoreContext_.Graph.Names().FileConf;
            auto comp = [&fileConf](TSourceDep left, TSourceDep right) -> bool {
                return NPath::CutAllTypes(fileConf.GetTargetName(left.To).GetTargetStr()) < NPath::CutAllTypes(fileConf.GetTargetName(right.To).GetTargetStr());
            };
            std::sort(Deps_.begin(), Deps_.end(), comp);
        }

        const TVector<TSourceDep>& Deps() const noexcept {
            return Deps_;
        }

        void PrintDepWithPath(IOutputStream& os, const TSourceDep& dep) const {
            const auto& graph = RestoreContext_.Graph;
            const auto& fileConf = graph.Names().FileConf;
            TStack<TStringBuf> fullPath;  // use for print path elements in reverse order
            fullPath.emplace(fileConf.GetName(dep.To).GetTargetStr());

            if (dep.PathId) {
                for (auto pathId = dep.PathId;; ++pathId) {
                    auto nodeId = Paths_[pathId];
                    auto nodeRef = graph.Get(TNodeId{ToUnderlying(nodeId) & 0x7fffffff});
                    TStringBuf name = graph.ToTargetStringBuf(nodeRef);
                    if (nodeRef->NodeType == EMNT_BuildCommand) {
                        name = SkipId(name);
                    }
                    fullPath.emplace(name);
                    if (ToUnderlying(nodeId) & LastPathNodeBit_) {
                        break;
                    }
                }
            }

            auto prev = fileConf.GetName(dep.From).GetTargetStr();
            while (fullPath.size()) {
                // emulate TDependencyPathFormatter (transitive_requirements_check.cpp) format here
                os << Endl << "    "sv << prev << " -> "sv << fullPath.top();
                prev = fullPath.top();
                fullPath.pop();
            }
        }

        TString StringDepWithPath(const TSourceDep& dep) const {
            TStringStream ss;
            PrintDepWithPath(ss, dep);
            return ss.Str();
        }

        TString StringDebugStatistics() const {
            TStringStream ss;
            ss << " deps_count=" << Deps_.size() << ";";
            ss << " paths_nodes_count=" << Paths_.size() << ";";
            for (const auto& [pathLength, count] : PathsLengthStat_) {
                ss << " paths_len" << pathLength << "_count=" << count << ";";
            }
            return ss.Str();
        }

    private:
        const TRestoreContext& RestoreContext_;
        TVector<TNodeId> SourcesNodesStack_;
        TVector<size_t> LateGlob_;
        TVector<TSourceDep> Deps_;
        static constexpr std::underlying_type_t<TNodeId> LastPathNodeBit_{0x80000000};
        // use queue for compact store path (not source-type nodes between TSourceDep(From..To) )
        // also for space economy use bit-marker for end of path (isolated node in path has set LastPathNodeBit)
        TDeque<TNodeId> Paths_;
        TMap<size_t, size_t> PathsLengthStat_;
        bool IsIsolatedProjectsHashChanged_;

        bool RequireModuleRecheck(const TModule* module) {
            // Recheck require if isolated projects changed OR module reconstructed
            // OR module has some errors in cache (some files may be removed, we must clear errors for they)
            // ELSE module isolated projects errors must be valid from cache
            return IsIsolatedProjectsHashChanged_ || !module->IsLoaded() || YConfHasMessagesByOwner(IslPrjs, module->GetId());
        }
    };

    class TRecurseIsolatedProjectsVisitor: public TNoReentryStatsConstVisitor<TVisitorStateItem<TEntryStatsData>> {
    public:
        using TBase = TNoReentryStatsConstVisitor<TVisitorStateItem<TEntryStatsData>>;
        using TState = typename TBase::TState;

        TRecurseIsolatedProjectsVisitor(const TRestoreContext& restoreContext)
            : RestoreContext_(restoreContext)
        {}

        bool AcceptDep(TState& state) {
            bool result = TBase::AcceptDep(state);
            const auto& dep = state.NextDep();
            return result && IsDirType(dep.To()->NodeType);
        }

        bool Enter(TState& state) {
            const auto& topNode = state.TopNode();
            if (!TBase::Enter(state)) {
                return false;
            }
            const auto& parentNode = state.ParentNode();
            if (parentNode.IsValid() && state.Parent()->CurDep().Value() == EDT_BuildFrom/* DEPENDS */) {
                auto& fileConf = RestoreContext_.Graph.Names().FileConf;
                auto path = fileConf.GetTargetName(topNode->ElemId).GetTargetStr();
                auto dir = fileConf.GetTargetName(parentNode->ElemId).GetTargetStr();
                auto makefile = RestoreContext_.Graph.Names().FileConf.GetStoredName(NPath::SmartJoin(dir, "ya.make"));
                RestoreContext_.Conf.IsolatedProjects.CheckStatementPath(NProps::DEPENDS, makefile, path);
            }
            return true;
        }

    private:
        const TRestoreContext& RestoreContext_;
    };
}

void TFoldersTree::Add(TStringBuf path) {
    TFoldersTree* folder{this};
    TStringBuf varPath{path};
    TPathSplitUnix splitter{varPath};
    for (const auto& folderName : splitter) {
        folder = folder->At(folderName);
    }
    folder->IsEndOfPath = true;
}

bool TFoldersTree::ExistsParentPathOf(TStringBuf path) const {
    const TFoldersTree* folder{this};
    TStringBuf varPath{path};
    TStringBuf shortFolderName;
    while (varPath.NextTok('/', shortFolderName)) {
        if (folder->IsEndOfPath) {
            return true;
        }
        folder = folder->At(shortFolderName);
        if (!folder) {
            return false;
        }
    }
    return folder->IsEndOfPath;
}

TVector<TString> TFoldersTree::GetPaths() const {
    TVector<TString> restoredPaths;
    TQueue<std::pair<TString, TFoldersTree>> queue;
    for (const auto& [dirName, subFolder] : Map) {
        queue.emplace(dirName, subFolder);
    }

    while (!queue.empty()) {
        auto [curPath, curSubFolder] = queue.front();
        queue.pop();

        if (curSubFolder.IsEndOfPath) {
            restoredPaths.emplace_back(curPath);
        }

        for (const auto& [dirName, subFolder] : curSubFolder.Map) {
            queue.emplace(curPath + '/' + dirName, subFolder);
        }
    }
    return restoredPaths;
}

void TIsolatedProjects::Load(const TFsPath& sourceRoot, const TVector<TStringBuf>& lists, MD5& confHash) {
    TProjectPathToSources projectToSources; // ordered projects + info from where we got it
    for (const auto path : lists) {
        try {
            TFileInput file(sourceRoot / path);
            TString content = file.ReadAll();
            confHash.Update(content.data(), content.size());
            LoadFromString(projectToSources, content, path);
        } catch (const TFileError& e) {
            YConfErr(BadFile) << "Error while reading blacklist file " << path << ": " << e.what() << Endl;
        }
    }
    LoadProjectsIgnoreIncluded(projectToSources);  // copy here (sorted) projectToSources to this->Projects (keep sorted order)
}

// check path not lead inside isolated project (or upper dir), or (if placed) makefile contain this path also placed in same project
void TIsolatedProjects::CheckStatementPath(TStringBuf statement, TFileView makefile, TStringBuf path) const {
    const TFoldersTree* folder = &FoldersTree_;
    TStringBuf varPath{path};  // variable consumed with NextTok iteration
    TStringBuf shortFolderName;
    while (varPath.NextTok('/', shortFolderName)) {
        const auto* subfolder = folder->At(shortFolderName);
        if (!subfolder) {
            if (folder->ExistsProject()) {
                // 'path' lead inside isolated_project
                CheckMakefilePlacedInProject(statement, makefile, *folder->GetProject(), path);
            }
            return;
        }
        folder = subfolder;
    }

    constexpr TStringBuf dependsStatement = NProps::DEPENDS;
    if (statement == dependsStatement) {
        if (folder->ExistsProject()) {
            CheckMakefilePlacedInProject(statement, makefile, *folder->GetProject(), path);
        }
        return;  // NOTE: ignore DEPENDS point to upper level (regarding isolated_projects) folder (disputable point, but now it generate too many false-positive errors)
    }

    // 'path' point to isolated_project(s) (or somewhere higher dir)
    CheckMakefilePlacedInProjects(statement, makefile, *folder, path);
}

void TIsolatedProjects::CheckMakefilePlacedInProjects(TStringBuf statement, TFileView makefile, const TFoldersTree& folder, TStringBuf path) const {
    if (folder.ExistsProject()) {
        CheckMakefilePlacedInProject(statement, makefile, *folder.GetProject(), path);
        return;
    }

    for (auto& [_, subFolder] : folder.Data()) {
        CheckMakefilePlacedInProjects(statement, makefile, subFolder, path);  // do not expect deep recurse here
    }
}

void TIsolatedProjects::CheckMakefilePlacedInProject(TStringBuf statement, TFileView makefile, const TString& project, TStringBuf path) const {
    auto makefileBuf = makefile.GetTargetStr();
    auto makefileStripped = NPath::CutAllTypes(makefileBuf);
    if (makefileStripped.length() >= project.length()
        && makefileStripped.compare(0, project.length(), project) == 0
        && (project.length() == makefileStripped.length() || makefileStripped[project.length()] == '/')
    ) {
        // has deps from isolated projects to itself, it's ok
    } else {
        auto scopedContext = MakeHolder<TScopedContext>(makefile);
        TStringStream dependencyDetails;
        dependencyDetails << makefileBuf << " [[alt1]]"sv << statement << "[[rst]] -> "sv << path;
        GenerateIsolatedProjectsError(project, makefile, dependencyDetails.Str());
    }
}

void TIsolatedProjects::CheckAll(const TRestoreContext& restoreContext, const TVector<TTarget>& startTargets, const TRestoreContext& recurseRestoreContext, const TVector<TTarget>& recurseStartTargets) const {
    const auto& conf = restoreContext.Conf;
    if (Empty()) {
        if (conf.IsIsolatedProjectsHashChanged()) {
            // Was some isolated projects, but now it empty, we must clear all isolated projects errors
            YConfErase(IslPrjs);
        }
        return; // no isolated projects, do nothing
    }
    ReportDeps(restoreContext, startTargets);
    ReportRecurseDeps(recurseRestoreContext, recurseStartTargets);
}

void TIsolatedProjects::ReportDeps(const TRestoreContext& restoreContext, const TVector<TTarget>& startTargets) const {
    const auto& conf = restoreContext.Conf;
    const auto& graph = restoreContext.Graph;
    TDepsCollectorVisitor visitor(restoreContext, conf.IsIsolatedProjectsHashChanged());
    IterateAll(
        graph,
        startTargets,
        visitor,
        [&conf](const TTarget& target) -> bool {
            return !(target.IsModuleTarget
                || (conf.SkipAllRecurses && !target.IsUserTarget)
                || target.IsNonDirTarget
                || (target.IsDependsTarget && !target.IsRecurseTarget && conf.SkipDepends)
            );
        }
    );

    YDebug() << "Isolated projects validator stats: " << visitor.StringDebugStatistics() << Endl;

    visitor.SortDepsByTo();

    // use parallel walk by isolated projects list & sorted deps for find deps leads to isolated projects and check it's isolation
    // NOTE: we use reverse iteration, because need process long project names before short (as example 'projecta' before 'project')
    auto itProject = Projects().rbegin();
    auto itDeps = visitor.Deps().rbegin();
    auto& fileConf = graph.Names().FileConf;
    while (itProject != Projects_.rend() && itDeps != visitor.Deps().rend()) {
        TStringBuf toName = fileConf.GetName(itDeps->To).CutAllTypes();
        auto res = toName.compare(0, itProject->length(), *itProject);
        if (res < 0) {
            ++itProject;
        } else if (res > 0) {
            ++itDeps;
        } else { // res == 0
            if (itProject->length() == toName.length() || toName[itProject->length()] == '/') {
                // found dependency lead inside isolated project
                const auto fromNameView = fileConf.GetTargetName(itDeps->From);
                TStringBuf fromName = NPath::CutType(fromNameView.GetTargetStr());
                if (fromName.compare(0, itProject->length(), *itProject) == 0
                    && (itProject->length() == fromName.length() || fromName[itProject->length()] == '/')
                ) {
                    // has deps from isolated projects to itself, it's ok
                } else {
                    THolder<TScopedContext> scopedContext{nullptr};
                    TFileView moduleView;
                    if (itDeps->ModuleId) {
                        moduleView = restoreContext.Graph.Names().FileConf.GetTargetName(itDeps->ModuleId);
                        scopedContext = MakeHolder<TScopedContext>(itDeps->ModuleId, moduleView.GetTargetStr());
                    }
                    GenerateIsolatedProjectsError(*itProject,  moduleView.IsValid() ? moduleView : fromNameView, visitor.StringDepWithPath(*itDeps));
                }
            }
            ++itDeps;
        }
    }
}

void TIsolatedProjects::ReportRecurseDeps(const TRestoreContext& recurseRestoreContext, const TVector<TTarget>& recurseStartTargets) const {
    TRecurseIsolatedProjectsVisitor recurseIsolateProjectsVisitor(recurseRestoreContext);
    IterateAll(recurseRestoreContext.Graph, recurseStartTargets, recurseIsolateProjectsVisitor);
}

void TIsolatedProjects::LoadFromString(TProjectPathToSources& projectToSources, TStringBuf content, TStringBuf file) {
    auto func = [this, &projectToSources, file](TStringBuf token) {
        const auto pos = token.find('#');
        token = StripString(token.Head(pos));
        if (!token.empty()) {
            TPathSplitUnix pathSplit(token);
            if (pathSplit.IsAbsolute) {
                OnAbsolutePath(token, file);
            } else if (pathSplit.empty()) {
                OnEmptyPath(token, file);
            } else if (pathSplit[0] == TStringBuf(".") || pathSplit[0] == TStringBuf("..")) {
                OnInvalidPath(token, file);
            } else {
                TString path = pathSplit.Reconstruct();
                if (path.empty()) {
                    OnEmptyPath(token, file);
                } else {
                    projectToSources[path].emplace(file);
                }
            }
        }
    };
    StringSplitter(content).SplitBySet("\n\r").SkipEmpty().Consume(func);
}

void TIsolatedProjects::OnAbsolutePath(TStringBuf path, TStringBuf file) const {
    YConfWarn(Syntax) << "Absolute path in isolated projects list file [[imp]]"
                      << ArcPath(file) << "[[rst]]. This path [[alt1]]" << path
                      << "[[rst]] will be skipped." << Endl;
}

void TIsolatedProjects::OnInvalidPath(TStringBuf path, TStringBuf file) const {
    YConfWarn(Syntax) << "Invalid path in isolated projects list file [[imp]]"
                      << ArcPath(file) << "[[rst]]. This path [[alt1]]" << path
                      << "[[rst]] will be skipped." << Endl;
}

void TIsolatedProjects::OnEmptyPath(TStringBuf path, TStringBuf file) const {
    YConfWarn(Syntax) << "Empty path in isolated projects list file [[imp]]"
                      << ArcPath(file) << "[[rst]]. This path [[alt1]]" << path
                      << "[[rst]] will be skipped." << Endl;
}

void TIsolatedProjects::OnIncludedProjectPath(TStringBuf includedPath, const THashSet<TStringBuf>& filesWithIncludedPath, TStringBuf path, const THashSet<TStringBuf>& files) const {
    TStringStream ss;
    ss << "Project [[alt1]]" << includedPath << "[[rst]] from project list file(s) ";
    for (auto& filename : filesWithIncludedPath) {
        if (filename.data() != filesWithIncludedPath.begin()->data()) {
            ss << ", ";
        }
        ss << "[[imp]]" << filename << "[[rst]]";
    }
    ss << " included into another isolated project: [[alt1]]" << path << "[[rst]] from project list file(s) ";
    for (auto& filename : files) {
        if (filename.data() != files.begin()->data()) {
            ss << ", ";
        }
        ss << "[[imp]]" << filename << "[[rst]]";
    }
    ss << ". This path: [[imp]]" << includedPath << "[[rst]] will be skipped.";
    YConfWarn(Syntax) << ss.Str() << Endl;
}

void TIsolatedProjects::GenerateIsolatedProjectsError(const TString& project, const TFileView&, const TString& dependencyDetails) const {
    YConfErr(IslPrjs) << "The project depends on [[alt1]]isolated project[[rst]] [[imp]]" << project << "[[rst]]" << Endl
                      << dependencyDetails << Endl;
}

void TIsolatedProjects::LoadProjectsIgnoreIncluded(const TProjectPathToSources& projectToSources) {
    // copy sorted projects to vector keeping order (& warn/skip included)
    Projects_.clear();
    Projects_.reserve(projectToSources.size());
    for (const auto& [path, srcPath] : projectToSources) {
        if (Projects_.size() && path.size() > Projects_.back().size() && path.StartsWith(Projects_.back()) && path[Projects_.back().size()] == '/') {
            OnIncludedProjectPath(path, srcPath, Projects_.back(), projectToSources.at(Projects_.back()));
        } else {
            Projects_.emplace_back(path);
        }
    }
    for (const auto& p : Projects_) {
        TStringBuf psv(p);
        TStringBuf shortFolderName;
        TFoldersTree* folder = &FoldersTree_;
        while (psv.NextTok('/', shortFolderName)) {
            Y_ASSERT(!folder->ExistsProject());  // including isolated_project to another isolated_project not allowed
            folder = folder->At(shortFolderName);
        }
        folder->SetProject(p);
    }
}
