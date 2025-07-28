#pragma once

#include <devtools/ymake/polexpr/expression.h>
#include <devtools/ymake/lang/macro_values.h>
#include <devtools/ymake/commands/mod_registry.h>

class TBuildConfiguration;

namespace NCommands {

    struct TSyntax {

        struct TTransformation;
        struct TCall;
        struct TBuiltinIf;

        // `TIdOrString` can only appear as a temporarily unclassified entry in a macro argument list;
        // for example, in `$MYMACRO(A B C)`, "A", "B", and "C" can be either names or values,
        // depending on whether respective parameters are positional or named
        // (`macro MYMACRO(A, B, C)...` vs. `macro MYMACRO(A[], B[], C[])...` etc.)
        struct TIdOrString {TString Value;};

        // given `macro SRCTEST(SRC)...`,
        // doing something like `.CMD=...$SRCTEST($SRC)...`
        // in the depths of the `SRCS` macro
        // should not result in the `SRC` argument
        // simply being defined as `$SRC` and infinitely expanded;
        // we use `TUnexpanded` to mark the original `$SRC` and similar breaker variables
        struct TUnexpanded {NPolexpr::EVarId Variable;};

        //
        //
        //

        using TTerm = std::variant<
            // proper data
            TMacroValues::TValue,
            NPolexpr::EVarId,
            TTransformation,
            TCall,
            TBuiltinIf,
            // temporary entries that should not appear in user-facing data
            TIdOrString,
            TUnexpanded
        >;
        using TArgument = std::vector<TTerm>;
        using TCommand = std::vector<TArgument>;
        using TScript = std::vector<TCommand>;

        struct TTransformation {
            struct TModifier {
                EMacroFunction Function;
                std::vector<TArgument> Arguments;
            };
            std::vector<TModifier> Mods;
            std::vector<TArgument> Body;
        };

        struct TCall {
            // ArgumentNames and Arguments are a structure-of-arrays equivalent of std::vector<std::pair<TStringBuf, TSyntax>>
            NPolexpr::EVarId Function;
            std::vector<TSyntax> Arguments;
            std::vector<TStringBuf> ArgumentNames;
        };

        struct TBuiltinIf {
            TSimpleSharedPtr<TTerm> Cond;
            TCommand Then;
            TCommand Else;
        };

        TScript Script;

    };

    TSyntax Parse(const TBuildConfiguration* conf, const TModRegistry& mods, TMacroValues& values, TStringBuf src);
    NPolexpr::TExpression Compile(const TModRegistry& mods, const TSyntax& src, TMacroValues& values);

}
