#include "graph_visitor.h"
#include "diag/exception.h"

#include <spdlog/spdlog.h>

namespace NYexport {

    static const THashMap<EAttrGroup, TGraphVisitor::ESemNameType> UsedAttrGroups = {
        {EAttrGroup::Root, TGraphVisitor::ESNT_RootAttr},
        {EAttrGroup::Directory, TGraphVisitor::ESNT_DirectoryAttr},
        {EAttrGroup::Target, TGraphVisitor::ESNT_TargetAttr},
        {EAttrGroup::Induced, TGraphVisitor::ESNT_InducedAttr},
    };

    TArgsConstraint AtLeast(size_t count) {
        return {
            .Type = EConstraintType::AtLeast,
            .Count = count};
    }
    TArgsConstraint MoreThan(size_t count) {
        return {
            .Type = EConstraintType::MoreThan,
            .Count = count};
    }
    TArgsConstraint Exact(size_t count) {
        return {
            .Type = EConstraintType::Exact,
            .Count = count};
    }

    TGraphVisitor::TGraphVisitor(TSpecBasedGenerator* generator)
        : Generator_(generator)
    {
        YEXPORT_VERIFY(Generator_, "Absent generator in graph visitor");

        SetupSemanticMapping(Generator_->GetGeneratorSpec());
    }

    bool TGraphVisitor::Enter(TState& state) {
        EnsureReady();

        state.Top().FreshNode = TBase::Enter(state);
        if (!state.Top().FreshNode) {
            return false;
        }

        auto onEnterRes = OnEnter(state);
        if (onEnterRes.has_value()) {
            return onEnterRes.value();
        }

        if (auto* currentProject = ProjectBuilder_->CurrentProject(); currentProject) {
            currentProject->SemsDumpDepth++;
        }
        if (auto* currentSubdir = ProjectBuilder_->CurrentSubdir(); currentSubdir) {
            currentSubdir->SemsDumpDepth++;
        }
        if (auto* currentTarget = ProjectBuilder_->CurrentTarget(); currentTarget) {
            currentTarget->SemsDumpDepth++;
        }

        const TSemNodeData& data = state.TopNode().Value();
        if (data.Sem.empty() || data.Sem.front().empty()) {
            FillSemsDump(data.Path, {}, {});
            return true;
        }

        bool isIgnored = false;
        const auto& semantics = Generator_->ApplyReplacement(data.Path, data.Sem);
        const TNodeSemantic* targetSemantic = nullptr;
        for (const auto& sem : semantics) {
            YEXPORT_VERIFY(!sem.empty(), "Empty semantic item on node '" << data.Path << "'");

            const auto& semName = sem[0];
            const auto semNameType = SemNameToType(semName);
            const auto semArgs = std::span{sem}.subspan(1);

            ProjectBuilder_->OnAttribute(semName);

            if (semNameType == ESNT_Ignored) {
                isIgnored = true;
                continue;
            } else if (semNameType == ESNT_Target) {
                if (!CheckArgs(semName, semArgs, AtLeast(2), data.Path)) {
                    continue;
                }
                if (targetSemantic != nullptr) {
                    spdlog::error("Node at path {} contains more than one target semantics. Node target will be overwritten", data.Path);
                }
                YEXPORT_VERIFY(targetSemantic == nullptr, "Node " << data.Path << " contains 2 target semantics: " << targetSemantic->at(0) << " & " << semName);
                targetSemantic = &sem;
            }
        }

        if (!isIgnored && targetSemantic) {
            const auto& sem = *targetSemantic;
            const auto& semName = sem[0];
            const auto semArgs = std::span{sem}.subspan(1);

            OnTargetNodeSemantic(state, semName, semArgs);
        }

        FillSemsDump(data.Path, data.Sem, semantics);

        for (const auto& sem : semantics) {
            const auto& semName = sem[0];
            const auto semNameType = SemNameToType(semName);
            const auto semArgs = std::span{sem}.subspan(1);

            if (&sem == targetSemantic || semNameType == ESNT_Ignored) {
                continue;
            }

            OnNodeSemanticPreOrder(state, semName, semNameType, semArgs);
        }
        return !isIgnored;
    }

    bool TGraphVisitor::HasErrors() const noexcept {
        return HasError_;
    }

    TProjectPtr TGraphVisitor::TakeFinalizedProject() {
        EnsureReady();
        return ProjectBuilder_->Finalize();
    }

    void TGraphVisitor::Leave(TState& state) {
        EnsureReady();
        OnLeave(state);

        if (!state.Top().FreshNode) {
            TBase::Leave(state);
            return;
        }
        const TSemNodeData& data = state.TopNode().Value();
        if (data.Sem.empty() || data.Sem.front().empty()) {
            TBase::Leave(state);
            return;
        }

        const auto& semantics = Generator_->ApplyReplacement(data.Path, data.Sem);
        for (const auto& sem : semantics) {
            const auto& semName = sem[0];
            const auto semNameType = SemNameToType(semName);
            const auto semArgs = std::span{sem}.subspan(1);

            OnNodeSemanticPostOrder(state, semName, semNameType, semArgs);
        }

        if (auto* currentProject = ProjectBuilder_->CurrentProject(); currentProject) {
            if (currentProject->SemsDumpDepth > 0) {
                currentProject->SemsDumpDepth--;
            }
        }
        if (auto* currentSubdir = ProjectBuilder_->CurrentSubdir(); currentSubdir) {
            if (currentSubdir->SemsDumpDepth > 0) {
                currentSubdir->SemsDumpDepth--;
            }
        }
        if (auto* currentTarget = ProjectBuilder_->CurrentTarget(); currentTarget) {
            if (currentTarget->SemsDumpDepth > 0) {
                currentTarget->SemsDumpDepth--;
            }
        }

        TBase::Leave(state);
    }

    void TGraphVisitor::Left(TState& state) {
        EnsureReady();
        OnLeft(state);
        TBase::Left(state);
    }

    void TGraphVisitor::OnError() {
        HasError_ = true;
    }

    void TGraphVisitor::AddSemanticMapping(const std::string& semName, ESemNameType type) {
        auto [it, created] = SemNameToType_.emplace(semName, type);
        YEXPORT_VERIFY(created, "Adding semantic mapping for " << semName << " which already exists");
    }

    TGraphVisitor::ESemNameType TGraphVisitor::SemNameToType(const std::string& semName) const {
        if (auto semNameIt = SemNameToType_.find(semName); semNameIt != SemNameToType_.end()) {
            return semNameIt->second;
        }
        return ESNT_Unknown;
    }

    bool TGraphVisitor::IsIgnored(const TSemGraph::TConstNodeRef& node) const {
        const TSemNodeData& data = node.Value();
        if (data.Sem.empty() || data.Sem.front().empty()) {
            return false;
        }

        bool isIgnored = false;
        const auto& semantics = Generator_->ApplyReplacement(data.Path, data.Sem);
        for (const auto& sem : semantics) {
            const auto& semName = sem[0];
            const auto semNameType = SemNameToType(semName);
            if (semNameType == ESNT_Ignored) {
                isIgnored = true;
                break;
            }
        }
        return isIgnored;
    }

    void TGraphVisitor::SetupSemanticMapping(const TGeneratorSpec& genspec) {
        SemNameToType_.emplace("IGNORED", ESNT_Ignored);
        for (const auto& [attrGroup, semNameType] : UsedAttrGroups) {
            if (const auto* attrs = genspec.AttrGroups.FindPtr(attrGroup)) {
                const TAttrGroup* inducedSpec = nullptr;
                if (attrGroup == EAttrGroup::Target) {
                    // We must skip duplicating induced attributes in target attr groups
                    inducedSpec = genspec.AttrGroups.FindPtr(EAttrGroup::Induced);
                }
                for (const auto& item : *attrs) {
                    if (inducedSpec && inducedSpec->contains(item.first)) {
                        continue; // skip duplicating induced attributes in target attr group
                    }
                    SemNameToType_.emplace(item.first, semNameType);
                }
            }
        }
        for (const auto& item : genspec.Targets) {
            SemNameToType_.emplace(item.first, ESNT_Target);
        }
    }

    void TGraphVisitor::EnsureReady() {
        YEXPORT_VERIFY(ProjectBuilder_, "Absent builder in graph visitor");
    }

    bool TGraphVisitor::CheckArgs(std::string_view sem, std::span<const std::string> args, TArgsConstraint constraint, const std::string& nodePath) {
        bool passed = false;
        std::string_view requirement;
        switch (constraint.Type) {
            case EConstraintType::Exact:
                passed = args.size() == constraint.Count;
                requirement = "exactly"sv;
                break;

            case EConstraintType::AtLeast:
                passed = args.size() >= constraint.Count;
                requirement = "at least"sv;
                break;

            case EConstraintType::MoreThan:
                passed = args.size() > constraint.Count;
                requirement = "more than"sv;
                break;
        }
        if (!passed) {
            const auto* subdir = ProjectBuilder_->CurrentSubdir();
            const auto* tgt = ProjectBuilder_->CurrentTarget();
            spdlog::error(
                "{}@{}: semantic {} at node {} requires {} {} arguments. Provided arguments count {}.\n\tProvided arguments are: {}",
                subdir ? subdir->Path.c_str() : "GLOBAL_SCOPE",
                tgt ? tgt->Name : "GLOBAL_SCOPE",
                sem,
                nodePath,
                requirement,
                constraint.Count,
                args.size(),
                fmt::join(args, ", "));
            OnError();
            return false;
        }
        return true;
    }

    void TGraphVisitor::FillSemsDump(const std::string& nodePath, const TNodeSemantics& graphSems, const TNodeSemantics& appliedSems) {
        if (!Generator_->DumpOpts().DumpSems && !Generator_->DebugOpts().DebugSems) {
            return;
        }
        auto Indent = [&](int depth) {
            return std::string(depth * 4, ' ');
        };
        auto semDump = [&](const TNodeSemantic& sem) {
            std::string dump;
            for (const auto& s: sem) {
                if (s != *sem.begin()) {
                    dump += " ";
                }
                dump += s;
            }
            return dump;
        };
        auto maxSize = std::max(graphSems.size(), appliedSems.size());
        auto* currentProject = ProjectBuilder_->CurrentProject();
        auto* currentSubdir = ProjectBuilder_->CurrentSubdir();
        auto* currentTarget = ProjectBuilder_->CurrentTarget();
        for (size_t i = 0; i < maxSize; ++i) {
            std::string graphSemDump;
            ESemNameType graphSemType = ESNT_Unknown;
            if (i < graphSems.size()) {
                const auto& graphSem = graphSems[i];
                graphSemType = SemNameToType(graphSem[0]);
                graphSemDump = semDump(graphSem);
            }
            std::string appliedSemDump;
            ESemNameType appliedSemType = ESNT_Unknown;
            if (i < appliedSems.size()) {
                const auto& appliedSem = appliedSems[i];
                appliedSemType = SemNameToType(appliedSem[0]);
                appliedSemDump = semDump(appliedSem);
            }
            auto isType = [&](ESemNameType semNameType) {
                return graphSemType == semNameType || appliedSemType == semNameType;
            };
            auto renderSemsDump = [&](TSemsDump* semsDump) {
                Y_ASSERT(semsDump);
                auto indent = Indent(semsDump->SemsDumpDepth);
                if (!semsDump->SemsPathAdded) {
                    semsDump->SemsDump += indent + "[ " + nodePath + " ]\n";
                    semsDump->SemsPathAdded = true;
                }
                semsDump->SemsDump += indent + "- " + appliedSemDump + (appliedSemDump == graphSemDump ? "" : " ( " + graphSemDump + " )") + "\n";
            };
            auto isRootAttr = isType(ESNT_RootAttr);
            auto isDirAttr = isType(ESNT_DirectoryAttr);
            auto isTarget = isType(ESNT_Target);
            if (isRootAttr || !currentTarget) { // all root semantics or without target add to Project
                renderSemsDump(currentProject);
            }
            if (isDirAttr || isTarget || !currentTarget) { // all directory semantics or without target add to Subdir
                renderSemsDump(currentSubdir);
            }
            if (!isRootAttr && !isDirAttr && !isTarget && currentTarget) {
                renderSemsDump(currentTarget);
            }
        }
        auto clearPathAdded = [&](TSemsDump* semsDump) {
            if (semsDump) {
                semsDump->SemsPathAdded = false;
            }
        };
        clearPathAdded(currentProject);
        clearPathAdded(currentSubdir);
        clearPathAdded(currentTarget);
    }
}
