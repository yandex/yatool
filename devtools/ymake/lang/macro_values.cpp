#include "macro_values.h"

#include <devtools/ymake/symbols/cmd_store.h>
#include <devtools/ymake/command_helpers.h>
#include <devtools/ymake/command_store.h>
#include <devtools/ymake/commands/evaluation.h>

#include <util/generic/overloaded.h>

#include <fmt/format.h>

namespace {

    TValueStorage::TData StringToBytes(std::string_view s) {
        return std::as_bytes(std::span(s.data(), s.size()));
    }

    std::string_view BytesToString(TValueStorage::TData data) {
        return std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
    }

    std::string ArrayToString(const std::vector<std::string>& data) {
        size_t size_est = 3 * (1 + data.size()); // (two digits + separator) * (count + sizes)
        for (auto& val : data)
            size_est += val.size();
        std::string result;
        result.reserve(size_est);
        fmt::format_to(std::back_inserter(result), "{} ", data.size());
        for (auto& val : data)
            fmt::format_to(std::back_inserter(result), "{} ", val.size());
        for (auto& val : data)
            result.append(val);
        return result;
    }

    std::vector<std::string> StringToArray(std::string_view data) {
        size_t desc = data.find(' ', 0);
        if (Y_UNLIKELY(desc == std::string_view::npos))
            throw std::runtime_error{"Bad string array"};
        size_t cnt = FromString<ui32>(data.substr(0, desc));
        if (Y_UNLIKELY(cnt > 10000))
            throw std::runtime_error{fmt::format("String array too large, {} items", cnt)};
        ++desc;
        size_t text = desc;
        for (size_t n = 0; n != cnt; ++n) {
            text = data.find(' ', text);
            if (Y_UNLIKELY(text == std::string_view::npos))
                throw std::runtime_error{"Bad string array"};
            ++text;
        }
        std::vector<std::string> result;
        result.reserve(cnt);
        for (size_t n = 0; n != cnt; ++n) {
            size_t next = data.find(' ', desc);
            size_t chunk_size = FromString<ui32>(data.substr(desc, next - desc));
            result.push_back(std::string(data.substr(text, chunk_size)));
            desc = ++next;
            text += chunk_size;
        }
        return result;
    }

}

//
//
//

std::string_view TMacroValues::GetVarName(NPolexpr::EVarId id) const {
    return Vars.GetName<TCmdView>(static_cast<ui32>(id)).GetStr();
}

NPolexpr::TConstId TMacroValues::InsertValue(const TValue& value) {
    return std::visit(TOverloaded{
        [](std::monostate) {
            return TInlineStorage::Put(EDataType::Void, 0);
        },
        [&](bool val) {
            return TInlineStorage::Put(EDataType::Bool, val);
        },
        [&](const TXString& val) {
            return Values.Put(EDataType::String, StringToBytes(val.Data));
        },
        [&](const TXStrings& val) {
            // TODO a general array storage
            return Values.Put(EDataType::StringArray, StringToBytes(ArrayToString(val.Data)));
        },
        [&](const TTool& val) {
            return Values.Put(EDataType::Tool, StringToBytes(val.Data));
        },
        [&](const TTools& val) {
            // TODO a general array storage
            return Values.Put(EDataType::ToolArray, StringToBytes(ArrayToString(val.Data)));
        },
        [&](const TResult& val) {
            return Values.Put(EDataType::Result, StringToBytes(val.Data));
        },
        [&](const TInput& val) {
            return TInlineStorage::Put(EDataType::Input, val.Coord);
        },
        [&](const TInputs& val) {
            // _obviously_, storing a proper array would be better,
            // but a naive implementation might actually be not as efficient;
            // TODO a general array storage
            auto encoded = fmt::format("{}", fmt::join(val.Coords, " "));
            return Values.Put(EDataType::InputArray, StringToBytes(encoded));
        },
        [&](const TOutput& val) {
            return TInlineStorage::Put(EDataType::Output, val.Coord);
        },
        [&](const TOutputs& val) {
            // _obviously_, storing a proper array would be better,
            // but a naive implementation might actually be not as efficient;
            // TODO a general array storage
            auto encoded = fmt::format("{}", fmt::join(val.Coords, " "));
            return Values.Put(EDataType::OutputArray, StringToBytes(encoded));
        },
        [&](const TGlobPattern& val) {
            return Values.Put(EDataType::Glob, StringToBytes(JoinArgs(std::span(val.Data), std::identity())));
        },
        [&](const TLegacyLateGlobPatterns& val) {
            return Values.Put(EDataType::LegacyLateGlob, StringToBytes(JoinArgs(std::span(val.Data), std::identity())));
        }
    }, value);
}

TMacroValues::TValue TMacroValues::GetValue(NPolexpr::TConstId id) const {
    switch (static_cast<EStorageType>(id.GetStorage())) {
        case EStorageType::Inline: {
            auto [type, data] = TInlineStorage::Get(id);
            switch (type) {
                case EDataType::Void:
                    return std::monostate();
                case EDataType::Bool:
                    return !!data;
                case EDataType::Input:
                    return TInput {.Coord = data};
                case EDataType::Output:
                    return TOutput {.Coord = data};
                default:
                    ; // fall out
            }
        }
        case EStorageType::Pool: {
            auto [type, data] = Values.Get(id);
            switch (type) {
                case EDataType::String:
                    return TXString{std::string(BytesToString(data))};
                case EDataType::StringArray:
                    return TXStrings{StringToArray(BytesToString(data))};
                case EDataType::Tool:
                    return TTool {.Data = std::string(BytesToString(data))};
                case EDataType::ToolArray:
                    return TTools {.Data = StringToArray(BytesToString(data))};
                case EDataType::Result:
                    return TResult {.Data = std::string(BytesToString(data))};
                case EDataType::InputArray: {
                    auto result = TInputs();
                    auto encoded = BytesToString(data);
                    for (auto coord : StringSplitter(encoded).Split(' ').SkipEmpty())
                        result.Coords.push_back(FromString<ui32>(coord.Token()));
                    return result;
                }
                case EDataType::OutputArray: {
                    auto result = TOutputs();
                    auto encoded = BytesToString(data);
                    for (auto coord : StringSplitter(encoded).Split(' ').SkipEmpty())
                        result.Coords.push_back(FromString<ui32>(coord.Token()));
                    return result;
                }
                case EDataType::Glob:
                    return TGlobPattern {.Data = SplitArgs(BytesToString(data))};
                case EDataType::LegacyLateGlob:
                    return TLegacyLateGlobPatterns {.Data = SplitArgs(BytesToString(data))};
                default:
                    ; // fall out
            }
        }
        default:
            ; // fall out
    }
    throw std::runtime_error{"Unknown value id"};
}

TStringBuf TMacroValues::Internalize(TStringBuf s) {
    return BytesToString(Values.Get(Values.Put(EDataType::String, StringToBytes(s))).second);
}

EDataType TMacroValues::GetType(NPolexpr::TConstId id) const {
    switch (static_cast<EStorageType>(id.GetStorage())) {
        case EStorageType::Inline: {
            auto [type, _] = TInlineStorage::Get(id);
            return type;
        }
        case EStorageType::Pool: {
            auto [type, _] = Values.Get(id);
            return type;
        }
        default:
            ; // fall out
    }
    throw std::runtime_error{"Unknown value id"};
}

void TMacroValues::Save(TMultiBlobBuilder& builder) const {
    auto macroValuesBuilder = MakeHolder<TMultiBlobBuilder>();
    Values.Save(*macroValuesBuilder);
    Vars.Save(*macroValuesBuilder);
    builder.AddBlob(macroValuesBuilder.Release());
}

void TMacroValues::Load(TBlob& multi) {
    TSubBlobs blob(multi);
    if (blob.size() != 2) {
        throw std::runtime_error{"Cannot load TMacroValues, number of received blobs " + ToString(blob.size()) + " expected 2"};
    }
    Values.LoadSingleBlob(blob[0]);
    Vars.LoadSingleBlob(blob[1]);
}
