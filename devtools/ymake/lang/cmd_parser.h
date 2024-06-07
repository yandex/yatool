#pragma once

#include <devtools/ymake/polexpr/expression.h>
#include <devtools/ymake/lang/macro_values.h>

class TBuildConfiguration;

namespace NCommands {

    struct TSyntax {

        // in variants, EVarId goes first to enable default ctors

        struct TSubstitution;
        struct TCall;

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
            NPolexpr::EVarId,
            NPolexpr::TConstId,
            TSubstitution,
            TCall,
            // temporary entries that should not appear in user-facing data
            TIdOrString,
            TUnexpanded
        >;
        using TArgument = std::vector<TTerm>;
        using TCommand = std::vector<TArgument>;
        using TCommands = std::vector<TCommand>;

        struct TSubstitution {
            struct TModifier {
                using TValueTerm = std::variant<NPolexpr::EVarId, NPolexpr::TConstId>;
                using TValue = TVector<TValueTerm>;
                using TValues = TVector<TValue>;
                EMacroFunctions Name;
                TValues Values;
            };
            TVector<TModifier> Mods;
            TCommand Body;
        };

        struct TCall {
            NPolexpr::EVarId Function;
            TVector<TSyntax> Arguments;
        };

        TCommands Commands;

    };

    TSyntax Parse(const TBuildConfiguration* conf, TMacroValues& values, TStringBuf src);
    NPolexpr::TExpression Compile(TMacroValues& values, const TSyntax& src);

}
