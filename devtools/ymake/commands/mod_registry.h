#pragma once

#include "evaluation.h"
#include <devtools/ymake/exec.h>

#include <util/generic/cast.h>

#include <span>
#include <ranges>

namespace NCommands {

    struct TModMetadata {
        EMacroFunction Id;
        TStringBuf Name;
        int Arity;
        bool Internal = false;
        bool MustPreevaluate = false;
        bool CanPreevaluate = false;
        bool CanEvaluate = false;
    };

    class TModImpl: public TModMetadata {
    public:
        TModImpl(TModMetadata metadata);
        ~TModImpl();
    public:
        virtual TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& unwrappedArgs
        ) const = 0;
        virtual TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const = 0;
    };

    class TModRegistry {
    public:
        TModRegistry();
        const TModImpl* At(EMacroFunction id) const {
            auto _id = ToUnderlying(id);
            Y_DEBUG_ABORT_UNLESS(0 <= _id && _id < Descriptions.size());
            return Descriptions[_id];
        }
        TMaybe<EMacroFunction> TryGetId(TStringBuf name) const {
            if (auto it = Index.find(name); it != Index.end())
                return it->second;
            else
                return {};
        }
    public:
        ui16 FuncArity(EMacroFunction func) const noexcept;
        NPolexpr::TFuncId Func2Id(EMacroFunction func) const noexcept;
        EMacroFunction Id2Func(NPolexpr::TFuncId id) const noexcept;
    private:
        std::array<const TModImpl*, ToUnderlying(EMacroFunction::Count)> Descriptions;
        THashMap<TStringBuf, EMacroFunction> Index;
    };

    //
    // legacy
    //

    TTermValue RenderTerms(std::span<const TTermValue> args);
    TTermValue RenderCat(std::span<const TTermValue> args);
    TTermValue RenderClear(std::span<const TTermValue> args);
    TTermValue RenderPre(std::span<const TTermValue> args);
    TTermValue RenderSuf(std::span<const TTermValue> args);
    TTermValue RenderJoin(std::span<const TTermValue> args);
    TTermValue RenderQuo(std::span<const TTermValue> args);
    TTermValue RenderQuoteEach(std::span<const TTermValue> args);
    TTermValue RenderToUpper(std::span<const TTermValue> args);
    TTermValue RenderToLower(std::span<const TTermValue> args);
    void RenderCwd(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args);
    void RenderStdout(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args);
    void RenderEnv(ICommandSequenceWriter* writer, const TEvalCtx& ctx, std::span<const TTermValue> args);
    void RenderKeyValue(const TEvalCtx& ctx, std::span<const TTermValue> args);
    void RenderLateOut(const TEvalCtx& ctx, std::span<const TTermValue> args);
    TTermValue RenderTagFilter(std::span<const TTermValue> args, bool exclude);
    TTermValue RenderTagCut(std::span<const TTermValue> args);
    TTermValue RenderRootRel(std::span<const TTermValue> args);
    TTermValue RenderCutPath(std::span<const TTermValue> args);
    TTermValue RenderLastExt(std::span<const TTermValue> args);
    TTermValue RenderExtFilter(std::span<const TTermValue> args);
    TTermValue RenderTODO1(std::span<const TTermValue> args);
    TTermValue RenderTODO2(std::span<const TTermValue> args);

}
