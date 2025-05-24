#pragma once

#include "../mod_registry.h"

namespace NCommands {

    template<typename T> const char *PrintableTypeName();
    // TMacroValues::TValue
    template<> [[maybe_unused]] inline const char *PrintableTypeName<std::monostate                       >() {return "[pre]Unit";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<bool                                 >() {return "[pre]Bool";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<std::string_view                     >() {return "[pre]String";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<std::vector<std::string_view>        >() {return "[pre]Strings";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TMacroValues::TTool                  >() {return "[pre]Tool";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TMacroValues::TTools                 >() {return "[pre]Tools";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TMacroValues::TResult                >() {return "[pre]Result";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TMacroValues::TInput                 >() {return "[pre]Input";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TMacroValues::TInputs                >() {return "[pre]Inputs";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TMacroValues::TOutput                >() {return "[pre]Output";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TMacroValues::TOutputs               >() {return "[pre]Outputs";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TMacroValues::TGlobPattern           >() {return "[pre]Glob";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TMacroValues::TLegacyLateGlobPatterns>() {return "[pre]LegacyGlob";}
    // TTermValue
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TTermError      >() {return "Error";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TTermNothing    >() {return "Unit";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TString         >() {return "String";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TVector<TString>>() {return "Strings";}
    template<> [[maybe_unused]] inline const char *PrintableTypeName<TTaggedStrings  >() {return "TaggedStrings";}

    class TBadArgType: public yexception {
    public:
        TBadArgType(TStringBuf fn, const auto& arg) {
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
        [[noreturn]] void FailArgCount(ssize_t count, std::string_view expected) const;
    };

}
