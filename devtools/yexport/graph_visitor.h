#pragma once

#include "sem_graph.h"
#include "project.h"
#include "spec_based_generator.h"

#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/common/uniq_vector.h>

#include <span>

namespace NYexport {

    enum class EConstraintType {
        AtLeast,
        MoreThan,
        Exact
    };
    struct TArgsConstraint {
        EConstraintType Type;
        size_t Count;
    };

    TArgsConstraint AtLeast(size_t count);
    TArgsConstraint MoreThan(size_t count);
    TArgsConstraint Exact(size_t count);

    struct TExtraStackData {
        TProject::TBuilder::TTargetHolder CurTargetHolder;
        bool FreshNode = false;
    };

    class TGraphVisitor
        : public TNoReentryVisitorBase<
              TVisitorStateItemBase,
              TSemGraphIteratorStateItem<TExtraStackData>,
              TGraphIteratorStateBase<TSemGraphIteratorStateItem<TExtraStackData>>> {
    public:
        using TBase = TNoReentryVisitorBase<
            TVisitorStateItemBase,
            TSemGraphIteratorStateItem<TExtraStackData>,
            TGraphIteratorStateBase<TSemGraphIteratorStateItem<TExtraStackData>>>;
        using TState = typename TBase::TState;

        enum ESemNameType {
            ESNT_Unknown = 0, // Semantic name not found in table
            ESNT_Target,      // Target for generator
            ESNT_RootAttr,    // Root of all targets attribute
            ESNT_PlatformAttr,// Platform attribute
            ESNT_DirectoryAttr,// Directory attribute
            ESNT_TargetAttr,  // Target for generator attribute
            ESNT_InducedAttr, // Target for generator induced attribute (add to list for parent node in graph)
            ESNT_Ignored,     // Must ignore this target for generator
        };

        TGraphVisitor(TSpecBasedGenerator* generator);

        void FillPlatformName(const std::string& platformName);
        bool HasErrors() const noexcept;
        TProjectPtr TakeFinalizedProject();

        // Functions from base
        bool Enter(TState& state);
        void Leave(TState& state);
        void Left(TState& state);

        TProjectBuilderPtr GetProjectBuilder() const { return ProjectBuilder_; }

    protected:
        virtual void OnTargetNodeSemantic(TState& state, const std::string& semName, const std::span<const std::string>& semArgs) = 0;
        virtual void OnNodeSemanticPreOrder(TState& /*state*/, const std::string& /*semName*/, ESemNameType /*semNameType*/, const std::span<const std::string>& /*semArgs*/, bool /*isIgnored*/) {};
        virtual void OnNodeSemanticPostOrder(TState& /*state*/, const std::string& /*semName*/, ESemNameType /*semNameType*/, const std::span<const std::string>& /*semArgs*/, bool /*isIgnored*/) {};
        virtual std::optional<bool> OnEnter(TState& /*state*/) { return {}; };
        virtual void OnLeave(TState& /*state*/) {};
        virtual void OnLeft(TState& /*state*/) {};

        void OnError();
        void AddSemanticMapping(const std::string& semName, ESemNameType type);
        bool CheckArgs(std::string_view sem, std::span<const std::string> args, TArgsConstraint constraint, const std::string& nodePath);
        ESemNameType SemNameToType(const std::string& semName) const;
        bool IsIgnored(const TSemGraph::TConstNodeRef& node) const;

        TProjectBuilderPtr ProjectBuilder_;
        TSpecBasedGenerator* Generator_;

    private:
        void MineSubdirTools(const TSemGraph::TConstNodeRef& node);
        void SetupSemanticMapping(const TGeneratorSpec& genspec);
        void EnsureReady();
        void FillSemsDump(const std::string& nodePath, const TNodeSemantics& graphSems, const TNodeSemantics& appliedSems);
        static std::string Indent(size_t depth);

        bool HasError_;
        THashMap<std::string, ESemNameType> SemNameToType_;
    };
}
