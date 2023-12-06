#include "dir_cleaner.h"

#include <devtools/ymake/compact_graph/query.h>

#include <util/generic/stack.h>

#include <spdlog/spdlog.h>

#include <set>

namespace NYexport {

namespace fs = std::filesystem;

using TNoReentryConstVisitor = TNoReentryVisitorBase<
    TVisitorStateItemBase,
    TGraphIteratorStateItemBase<true, TSemGraph>,
    TGraphIteratorStateBase<TGraphIteratorStateItemBase<true, TSemGraph>>>;

void TDirCleaner::CollectDirs(const TSemGraph& graph, const TVector<TNodeId>& startDirs) {
    const std::set<TStringBuf> remove = [&] {
        std::set<TStringBuf> remove;
        TNoReentryConstVisitor::TState state;
        TNoReentryConstVisitor visitor;
        for (TNodeId start: startDirs) {
            TSemGraphDepthIterator<TNoReentryConstVisitor::TState, TNoReentryConstVisitor> it{graph, state, visitor};
            for (bool res = it.Init(start); res; res = it.Next()) {
                if (!IsModule(*it)) {
                    continue;
                }

                if (AnyOf(it->Node()->Sem, [](const auto& sem) { return !sem.empty() && (sem.front() == "IGNORED"); })) {
                    remove.insert(NPath::Parent(NPath::CutType(it->Node()->Path)));
                }
            }
        }
        return remove;
    }();

    TNoReentryConstVisitor::TState state;
    TNoReentryConstVisitor visitor;
    for (TNodeId start: startDirs) {
        TSemGraphDepthIterator<TNoReentryConstVisitor::TState, TNoReentryConstVisitor> it{graph, state, visitor};
        for (bool res = it.Init(start); res; res = it.Next()) {
            if (!IsModule(*it) || AnyOf(it->Node()->Sem, [](const auto& sem) { return !sem.empty() && (sem.front() == "IGNORED"); })) {
                continue;
            }

            const auto dir = NPath::Parent(NPath::CutType(it->Node()->Path));
            const auto removeIt = remove.lower_bound(dir);
            if (removeIt != remove.end() && dir.starts_with(*removeIt)) {
                fs::path path{std::string{dir}};
                SubdirsToKeep.insert(std::move(path));
            }
        }
    }

    for (TStringBuf dir: remove) {
        DirsToRemove.insert(std::string{dir});
    }
}

void TDirCleaner::Clean(TExportFileManager& exportFileManager) const {
    struct TStackFrame {
        fs::directory_iterator Tail;
        bool Keep = false;

        fs::path RelativeTo(const fs::path& exportRoot) {
            if (Tail == fs::directory_iterator{}){
                return {};
            }
            return  Tail->path().lexically_relative(exportRoot);
        }
    };

    for (const auto& remove: DirsToRemove) {
        if (SubdirsToKeep.contains(remove)) {
            continue;
        }
        if (!exportFileManager.Exists(remove)) {
            continue;
        }

        auto absPath = exportFileManager.GetExportRoot() / remove;
        TStack<TStackFrame> stack;
        stack.push({.Tail = fs::directory_iterator{std::move(absPath)}, .Keep = false});

        while (!stack.empty()) {
            auto &top = stack.top();
            if (top.Tail == fs::directory_iterator{}) {
                const bool keep = top.Keep;
                stack.pop();
                if (!stack.empty()) {
                    if (keep) {
                        stack.top().Keep = true;
                    } else {
                        spdlog::info("removing ignored dir: {}", stack.top().Tail->path().string());
                        exportFileManager.Remove(stack.top().RelativeTo(exportFileManager.GetExportRoot()));
                    }
                    ++stack.top().Tail;
                }
                continue;
            }

            for (; top.Tail != fs::directory_iterator{}; ++top.Tail) {
                if (top.Tail->is_directory()) {
                    if (SubdirsToKeep.contains(top.RelativeTo(exportFileManager.GetExportRoot()))) {
                        top.Keep = true;
                    } else {
                        stack.push({.Tail = fs::directory_iterator{top.Tail->path()}, .Keep = false});
                    }
                    break;
                }
                exportFileManager.Remove(top.RelativeTo(exportFileManager.GetExportRoot()));
            }
        }
    }
}

}
