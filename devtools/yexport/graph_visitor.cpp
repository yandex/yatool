#include "graph_visitor.h"
#include "diag/exception.h"

#include <spdlog/spdlog.h>

namespace NYexport {

    static const THashMap<EAttributeGroup, TGraphVisitor::ESemNameType> UsedAttrGroups = {
        {EAttributeGroup::Induced, TGraphVisitor::ESNT_InducedAttr},
        {EAttributeGroup::Target, TGraphVisitor::ESNT_TargetAttr},
        {EAttributeGroup::Root, TGraphVisitor::ESNT_RootAttr},
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
        if (onEnterRes) {
            return *onEnterRes;
        }

        const TSemNodeData& data = state.TopNode().Value();
        if (data.Sem.empty() || data.Sem.front().empty()) {
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

    std::optional<bool> TGraphVisitor::OnEnter(TState&) {
        return {};
    }

    void TGraphVisitor::SetupSemanticMapping(const TGeneratorSpec& genspec) {
        SemNameToType_.emplace("IGNORED", ESNT_Ignored);
        for (const auto& [attrGroup, semNameType] : UsedAttrGroups) {
            if (const auto* attrs = genspec.AttrGroups.FindPtr(attrGroup)) {
                const TAttributeGroup* inducedSpec = nullptr;
                if (attrGroup == EAttributeGroup::Target) {
                    // We must skip duplicating induced attributes in target attr groups
                    inducedSpec = genspec.AttrGroups.FindPtr(EAttributeGroup::Induced);
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

    void TGraphVisitor::OnTargetNodeSemantic(TState&, const std::string&, const std::span<const std::string>&) {
    }

    void TGraphVisitor::OnNodeSemanticPreOrder(TState&, const std::string&, ESemNameType, const std::span<const std::string>&) {
    }

    void TGraphVisitor::OnNodeSemanticPostOrder(TState&, const std::string&, ESemNameType, const std::span<const std::string>&) {
    }

    void TGraphVisitor::OnLeave(TState&) {
    }

    void TGraphVisitor::OnLeft(TState&) {
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
};
