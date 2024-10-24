#pragma once

#include <devtools/ymake/polexpr/ids.h>
#include <devtools/ymake/symbols/name_store.h>

#include <string_view>
#include <variant>

enum class EMacroFunction: ui32 {
    // constructors
    Cmds,
    Args,
    Terms,
    // utilities
    Cat,
    // modifiers
    Input,
    Output,
    Tmp,
    Tool,
    Hide,
    Clear,
    Pre,
    Suf,
    HasDefaultExt,
    Join,
    Quo,
    QuoteEach,
    ToUpper,
    ToLower,
    Cwd,
    AsStdout,
    SetEnv,
    RootRel,
    CutPath,
    CutExt,
    LastExt,
    ExtFilter,
    KeyValue,
    LateOut,
    TagsIn,
    TagsOut,
    TagsCut,
    Glob,
    // markers
    Context, // input-only, preevaluated
    NoAutoSrc, // output-only, preevaluated
    NoRel, // output-only, preevaluated
    ResolveToBinDir, // output-only, preevaluated
    //
    Count
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
        ST_GLOB,
        ST_INPUT_ARRAYS,
    };

    NPolexpr::TConstId InsertStr(std::string_view val) { return NPolexpr::TConstId(ST_LITERALS, Strings.Add(val)); }
    NPolexpr::EVarId InsertVar(std::string_view name) { return static_cast<NPolexpr::EVarId>(Vars.Add(name)); }

    std::string_view GetVarName(NPolexpr::EVarId id) const;

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
