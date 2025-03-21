#include "macro_values.h"

#include <devtools/ymake/symbols/cmd_store.h>
#include <devtools/ymake/command_helpers.h>
#include <devtools/ymake/command_store.h>
#include <devtools/ymake/commands/evaluation.h>

#include <util/generic/overloaded.h>

#include <fmt/format.h>

namespace {

    constexpr ui32 InvalidNameId = 0; // see TNameStore::CheckId, TNameStore::Clear

    TString ArrayToString(const std::vector<std::string_view>& data) {
        size_t size_est = 3 * (1 + data.size()); // (two digits + separator) * (count + sizes)
        for (auto& val : data)
            size_est += val.size();
        TString result;
        result.reserve(size_est);
        fmt::format_to(std::back_inserter(result), "{} ", data.size());
        for (auto& val : data)
            fmt::format_to(std::back_inserter(result), "{} ", val.size());
        for (auto& val : data)
            result.append(val);
        return result;
    }

    std::vector<std::string_view> StringToArray(TStringBuf data) {
        size_t desc = data.find(' ', 0);
        if (Y_UNLIKELY(desc == TStringBuf::npos))
            throw std::runtime_error{"Bad string array"};
        size_t cnt = FromString<ui32>(data.substr(0, desc));
        if (Y_UNLIKELY(cnt > 10000))
            throw std::runtime_error{fmt::format("String array too large, {} items", cnt)};
        ++desc;
        size_t text = desc;
        for (size_t n = 0; n != cnt; ++n) {
            text = data.find(' ', text);
            if (Y_UNLIKELY(text == TStringBuf::npos))
                throw std::runtime_error{"Bad string array"};
            ++text;
        }
        std::vector<std::string_view> result;
        result.reserve(cnt);
        for (size_t n = 0; n != cnt; ++n) {
            size_t next = data.find(' ', desc);
            size_t chunk_size = FromString<ui32>(data.substr(desc, next - desc));
            result.push_back(data.substr(text, chunk_size));
            desc = ++next;
            text += chunk_size;
        }
        return result;
    }

}

std::string_view TMacroValues::GetVarName(NPolexpr::EVarId id) const {
    return Vars.GetName<TCmdView>(static_cast<ui32>(id)).GetStr();
}

NPolexpr::TConstId TMacroValues::InsertValue(const TValue& value) {
    return std::visit(TOverloaded{
        [](std::monostate) {
            return NPolexpr::TConstId(ST_LITERALS, InvalidNameId);
        },
        [&](bool val) {
            return NPolexpr::TConstId(ST_BOOL, val);
        },
        [&](std::string_view val) {
            return NPolexpr::TConstId(ST_LITERALS, Strings.Add(val));
        },
        [&](const std::vector<std::string_view>& val) {
            // TODO a general array storage
            return NPolexpr::TConstId(ST_STRING_ARRAYS, Strings.Add(ArrayToString(val)));
        },
        [&](const TTool& val) {
            return NPolexpr::TConstId(ST_TOOLS, Refs.Add(val.Data));
        },
        [&](const TInput& val) {
            return NPolexpr::TConstId(ST_INPUTS, val.Coord);
        },
        [&](const TInputs& val) {
            // _obviously_, storing a proper array would be better,
            // but a naive implementation might actually be not as efficient;
            // TODO a general array storage
            auto encoded = fmt::format("{}", fmt::join(val.Coords, " "));
            return NPolexpr::TConstId(ST_INPUT_ARRAYS, Strings.Add(encoded));
        },
        [&](const TOutput& val) {
            return NPolexpr::TConstId(ST_OUTPUTS, val.Coord);
        },
        [&](const TOutputs& val) {
            // _obviously_, storing a proper array would be better,
            // but a naive implementation might actually be not as efficient;
            // TODO a general array storage
            auto encoded = fmt::format("{}", fmt::join(val.Coords, " "));
            return NPolexpr::TConstId(ST_OUTPUT_ARRAYS, Strings.Add(encoded));
        },
        [&](const TGlobPattern& val) {
            return NPolexpr::TConstId(ST_GLOB, Strings.Add(JoinArgs(std::span(val.Data), std::identity())));
        },
        [&](const TLegacyLateGlobPatterns& val) {
            return NPolexpr::TConstId(ST_LEGACY_LATE_GLOB, Strings.Add(JoinArgs(std::span(val.Data), std::identity())));
        }
    }, value);
}

TMacroValues::TValue TMacroValues::GetValue(NPolexpr::TConstId id) const {
    switch (id.GetStorage()) {
        case ST_BOOL:
            return !!id.GetIdx();
        case ST_LITERALS: {
            auto idx = id.GetIdx();
            if (Y_UNLIKELY(idx == InvalidNameId))
                return std::monostate();
            return Strings.GetName<TCmdView>(idx).GetStr();
        }
        case ST_STRING_ARRAYS:
            return StringToArray(Strings.GetName<TCmdView>(id.GetIdx()).GetStr());
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
        case ST_OUTPUT_ARRAYS: {
            auto idx = id.GetIdx();
            auto result = TOutputs();
            auto encoded = Strings.GetName<TCmdView>(idx).GetStr();
            for (auto coord : StringSplitter(encoded).Split(' ').SkipEmpty())
                result.Coords.push_back(FromString<ui32>(coord.Token()));
            return result;
        }
        case ST_GLOB:
            return TGlobPattern {.Data = SplitArgs(Strings.GetName<TCmdView>(id.GetIdx()).GetStr())};
        case ST_LEGACY_LATE_GLOB:
            return TLegacyLateGlobPatterns {.Data = SplitArgs(Strings.GetName<TCmdView>(id.GetIdx()).GetStr())};
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
