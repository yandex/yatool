#pragma once

#include "../mod_registry.h"

namespace NCommands {

    template<typename T> const char *PrintableTypeName();
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TTermNothing    >() {return "Unit";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TString         >() {return "String";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TVector<TString>>() {return "Strings";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TTaggedStrings  >() {return "TaggedStrings";}

    class TBadArgType: public yexception {
    public:
        TBadArgType(TStringBuf fn, auto& arg) {
            *this << "type " << PrintableTypeName<std::remove_cvref_t<decltype(arg)>>() << " is not supported by " << fn;
        }
    };

    class TBasicModImpl: public TModImpl {
    public:
        TBasicModImpl(TModMetadata metadata): TModImpl(metadata) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& unwrappedArgs
        ) const override {
            Y_ABORT();
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            Y_ABORT();
        }
    protected:
        template<std::ranges::range A>
        void CheckArgCount(A& args) const {
            CheckArgCount(std::ssize(args));
        }
        void CheckArgCount(ssize_t count) const;
    };

}
