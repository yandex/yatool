#pragma once

#include "action_binding.h"
#include "action_glob.h"
#include "action_input.h"
#include "../macro_vars.h"

#include <util/generic/ptr.h>

class TAddDepAdaptor;
class TBuildConfiguration;
struct TCommandInfo;
class TDepGraph;
class TModule;
class TModuleBuilder;
struct TNodeAddCtx;
class TUpdIter;

namespace NPolexpr {
    class TExpression;
}

// Model-private compatibility encoder for command-related graph storage.
//
// The producer-facing action interface must not expose these choices.  They
// remain explicit here while the existing graph representation distinguishes
// structured actions, structured bindings and legacy command-like values.
class TActionGraphEncoder {
public:
    struct TImportedCommandExpression {
        TCmdElemId Id;
        TStringBuf Reference;
    };

    class TPreparedSubmission final : public IActionInputModelSink {
    public:
        TFileElemId InternLogicalPath(TStringBuf path) override;
        void AcceptResolvedInput(const TResolvedActionInput& input) override;

    private:
        friend class TActionGraphEncoder;

        TPreparedSubmission(const TActionGraphEncoder& encoder, TAddDepAdaptor& storage);

        const TActionGraphEncoder* Encoder_;
        TAddDepAdaptor* Storage_;
    };

    enum class EStorageFormat {
        Legacy,
        Structured,
    };

    enum class EExpressionRole {
        Action,
        Binding,
    };

    enum class EBindingPlacement {
        Local,
        GlobalCompatibility,
    };

    enum class EActionEncodingResult {
        Failed,
        Complete,
        NeedsCompletion,
    };

    TActionGraphEncoder(
        const TBuildConfiguration& conf,
        TDepGraph& graph,
        TUpdIter& updIter,
        TModule* module
    );

    // Compatibility constructor for traversal continuations which still retain
    // TCommandInfo.  New submission initiators should pass model context
    // explicitly through the constructor above.
    explicit TActionGraphEncoder(TCommandInfo& commandInfo);

    TPreparedSubmission PrepareActionSubmission(TAddDepAdaptor& storage) const;
    TPreparedSubmission PrepareVariableSubmission(TAddDepAdaptor& storage, TCmdElemId variable) const;
    TActionGlobEvaluationContext MakeGlobEvaluationContext() const;

    EActionEncodingResult EncodeActionSubmission(
        TAutoPtr<TCommandInfo>& commandInfo,
        TModuleBuilder& moduleBuilder,
        TPreparedSubmission& submission,
        IActionGlobEvaluator& globEvaluator,
        bool finalTargetCommand
    ) const;

    void CompleteActionSubmission(
        TAutoPtr<TCommandInfo>& commandInfo,
        TModuleBuilder& moduleBuilder,
        TPreparedSubmission& submission,
        bool finalTargetCommand
    ) const;

    bool CompleteVariableSubmission(
        TCommandInfo& commandInfo,
        TPreparedSubmission& submission
    ) const;

    TVector<TStringBuf> ConfigurationBindingVariables(
        const TVector<TDepsCacheId>& variableLists
    ) const;

    void AddConfigurationBinding(
        TCompiledBindingExpression&& binding,
        TNodeAddCtx& submission
    ) const;

    // Compatibility import for an expression already compiled by the
    // producer. The model owns command-store interning and its name-table
    // mapping; a later boundary can replace TExpression with lowered TSyntax.
    TImportedCommandExpression ImportCommandExpression(
        NPolexpr::TExpression&& expression
    ) const;

    TCmdElemId InternCommand(
        const TYVar& value,
        EStorageFormat format,
        EExpressionRole role
    ) const;

    void RegisterCommand(
        const TYVar& value,
        TCmdElemId elemId,
        EStorageFormat format,
        EExpressionRole role
    ) const;

    void AttachAction(TAddDepAdaptor& action, TCmdElemId command) const;
    void AttachBinding(
        TAddDepAdaptor& owner,
        TCmdElemId binding,
        EBindingPlacement placement
    ) const;

    void RecordReservedVariable(const TYVar& owner, TStringBuf name) const;
    void RecordLateOutput(const TYVar& owner, TStringBuf expression) const;
    void AttachToolDirectory(const TYVar& owner, TStringBuf directory) const;
    bool AttachLegacyBinding(const TYVar& owner, TCmdElemId binding) const;
    void AttachLegacyAction(const TYVar& owner, TCmdElemId action) const;
    void AttachMissingBinding(const TYVar& owner, TStringBuf name) const;
    void RecordGlobalBindingUse(TStringBuf name) const;
    void AttachGlobalBinding(
        TCompiledGlobalBinding&& binding,
        TPreparedSubmission& submission
    ) const;

private:
    TFileElemId InternLogicalPath(TStringBuf path) const;
    void RecordResolvedInput(TAddDepAdaptor& storage, const TResolvedActionInput& input) const;
    void RecordInputResolution(const TInputResolutionRecord& resolution) const;
    void EncodeGlobInput(TAddDepAdaptor& node, TEvaluatedActionGlob&& glob) const;
    void AttachModuleInput(
        TModuleBuilder& moduleBuilder,
        TVarStrEx& input,
        TAddDepAdaptor& submission,
        TElemId group
    ) const;
    void AttachVariableInput(
        TVarStrEx& input,
        TAddDepAdaptor& submission
    ) const;
    const TBuildConfiguration& Conf_;
    TDepGraph& Graph_;
    TUpdIter& UpdIter_;
    TModule* Module_;
};
