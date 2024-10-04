#include "macro_values.h"

#include <devtools/ymake/symbols/cmd_store.h>
#include <devtools/ymake/command_store.h>
#include <devtools/ymake/commands/evaluation.h>

#include <fmt/format.h>

std::string_view TMacroValues::GetVarName(NPolexpr::EVarId id) const {
    return Vars.GetName<TCmdView>(static_cast<ui32>(id)).GetStr();
}

NPolexpr::TConstId TMacroValues::InsertValue(const TValue& value) {
    return std::visit([this](auto&& val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::string_view>)
            return NPolexpr::TConstId(ST_LITERALS, Strings.Add(val));
        else if constexpr (std::is_same_v<T, TTool>)
            return NPolexpr::TConstId(ST_TOOLS, Refs.Add(val.Data));
        else if constexpr (std::is_same_v<T, TInput>)
            return NPolexpr::TConstId(ST_INPUTS, val.Coord);
        else if constexpr (std::is_same_v<T, TInputs>) {
            // _obviously_, storing a proper array would be better,
            // but a naive implementation might actually be not as efficient;
            // TODO a general array storage
            auto encoded = fmt::format("{}", fmt::join(val.Coords, " "));
            return NPolexpr::TConstId(ST_INPUT_ARRAYS, Strings.Add(encoded));
        } else if constexpr (std::is_same_v<T, TOutput>)
            return NPolexpr::TConstId(ST_OUTPUTS, val.Coord);
        else if constexpr (std::is_same_v<T, TGlobPattern>)
            return NPolexpr::TConstId(ST_GLOB, Strings.Add(val.Data));
    }, value);
}

TMacroValues::TValue TMacroValues::GetValue(NPolexpr::TConstId id) const {
    switch (id.GetStorage()) {
        case ST_LITERALS:
            return Strings.GetName<TCmdView>(id.GetIdx()).GetStr();
        case ST_TOOLS:
            return TTool {.Data = Refs.GetName<TCmdView>(id.GetIdx()).GetStr()};
        case ST_INPUTS: {
            auto idx = id.GetIdx();
            return TInput {.Coord = idx};
        }
        case ST_INPUT_ARRAYS: {
            auto idx = id.GetIdx();
            auto result = TInputs();
            auto encoded = Strings.GetName<TCmdView>(idx).GetStr();
            for (auto coord : StringSplitter(encoded).Split(' ').SkipEmpty())
                result.Coords.push_back(FromString<ui32>(coord.Token()));
            return result;
        }
        case ST_OUTPUTS:
            return TOutput {.Coord = id.GetIdx()};
        case ST_GLOB:
            return TGlobPattern {.Data = Strings.GetName<TCmdView>(id.GetIdx()).GetStr()};
        default:
            throw std::runtime_error{"Unknown storage id"};
    }
}

void TMacroValues::Save(TMultiBlobBuilder& builder) const {
    auto macroValuesBuilder = MakeHolder<TMultiBlobBuilder>();
    CmdPattern.Save(*macroValuesBuilder);
    Strings.Save(*macroValuesBuilder);
    Refs.Save(*macroValuesBuilder);
    Vars.Save(*macroValuesBuilder);
    builder.AddBlob(macroValuesBuilder.Release());
}

void TMacroValues::Load(TBlob& multi) {
    TSubBlobs blob(multi);
    if (blob.size() != 4) {
        throw std::runtime_error{"Cannot load TMacroValues, number of received blobs " + ToString(blob.size()) + " expected 4"};
    }
    CmdPattern.LoadSingleBlob(blob[0]);
    Strings.LoadSingleBlob(blob[1]);
    Refs.LoadSingleBlob(blob[2]);
    Vars.LoadSingleBlob(blob[3]);
}
