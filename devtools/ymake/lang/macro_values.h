#pragma once

#include <devtools/ymake/polexpr/ids.h>
#include <devtools/ymake/symbols/name_store.h>

#include <string_view>
#include <variant>

enum class EMacroFunctions: ui32 {
    // constructors
    Cmds,
    Args,
    Terms,
    // utilities
    Cat,
    // modifiers
    Input,
    Output,
    Tool,
    Hide,
    Clear,
    Pre,
    Suf,
    Quo,
    SetEnv,
    CutExt,
    LastExt,
    ExtFilter,
    KeyValue,
    Glob,
    TODO1,
    TODO2,
    // markers
    NoAutoSrc, // output-only, preevaluated
};

class TMacroValues {
public:
    struct TTool {
        std::string_view Data;
        bool operator==(const TTool&) const = default;
    };
    struct TInput {
        ui32 Coord;
        bool operator==(const TInput&) const = default;
    };
    struct TInputs {
        TVector<ui32> Coords;
        bool operator==(const TInputs&) const = default;
    };
    struct TOutput {
        ui32 Coord;
        bool operator==(const TOutput&) const = default;
    };
    struct TGlobPattern {
        std::string_view Data;
        bool operator==(const TGlobPattern&) const = default;
    };
    using TValue = std::variant<std::string_view, TTool, TInput, TInputs, TOutput, TGlobPattern>;

    enum EStorageType {
        ST_LITERALS,
        ST_TOOLS,
        ST_INPUTS,
        ST_OUTPUTS,
        ST_GLOB
    };

    NPolexpr::TConstId InsertStr(std::string_view val) { return NPolexpr::TConstId(ST_LITERALS, Strings.Add(val)); }
    NPolexpr::EVarId InsertVar(std::string_view name) { return static_cast<NPolexpr::EVarId>(Vars.Add(name)); }

    std::string_view GetVarName(NPolexpr::EVarId id) const;
    ui16 FuncArity(EMacroFunctions func) const noexcept;
    NPolexpr::TFuncId Func2Id(EMacroFunctions func) const noexcept;
    EMacroFunctions Id2Func(NPolexpr::TFuncId id) const noexcept;

    NPolexpr::TConstId InsertValue(const TValue& value);
    TValue GetValue(NPolexpr::TConstId id) const;

    void Save(TMultiBlobBuilder& builder) const;
    void Load(TBlob& multi);

private:
    TNameStore CmdPattern;
    TNameStore Strings;
    TNameStore Refs;
    TNameStore Vars;
};
