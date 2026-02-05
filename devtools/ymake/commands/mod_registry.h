#pragma once

#include "evaluation.h"
#include <devtools/ymake/exec.h>

#include <util/generic/cast.h>

#include <span>

namespace NCommands {

    struct TModMetadata {
        EMacroFunction Id;
        TStringBuf Name;
        int Arity;
        bool Internal = false;
        bool MustPreevaluate = false;
    };

    struct TPseudoException {};
    struct TNotSupported: public TPseudoException {};

    class TModImpl: public TModMetadata {
    public:
        TModImpl(TModMetadata metadata);
        ~TModImpl();
    public:
        virtual TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const {
            throw TNotSupported();
        }
        virtual TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const {
            throw TNotSupported();
        }
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
        ui16 FuncArity(EMacroFunction func) const;
        NPolexpr::TFuncId Func2Id(EMacroFunction func) const;
        EMacroFunction Id2Func(NPolexpr::TFuncId id) const;
    private:
        std::array<const TModImpl*, ToUnderlying(EMacroFunction::Count)> Descriptions;
        THashMap<TStringBuf, EMacroFunction> Index;
    };

}
