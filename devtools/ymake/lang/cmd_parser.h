#pragma once

#include <devtools/ymake/polexpr/expression.h>
#include <devtools/ymake/lang/macro_values.h>

namespace NCommands {

    struct TSyntax {

        // in variants, EVarId goes first to enable default ctors

        struct TSubstitution;
        using TTerm = std::variant<NPolexpr::EVarId, NPolexpr::TConstId, TSubstitution>;
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

        TCommands Commands;

    };

    TSyntax Parse(TMacroValues& values, TStringBuf src);
    NPolexpr::TExpression Compile(TMacroValues& values, const TSyntax& src);

}
