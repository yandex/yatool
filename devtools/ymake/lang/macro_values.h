#pragma once

#include "value_storage.h"

#include <devtools/ymake/polexpr/ids.h>
#include <devtools/ymake/symbols/name_store.h>

#include <string_view>
#include <variant>

//
// mod list evolution policy:
// * id-value to functionality mapping must not change, except for occasional "garbage collection"
// * in particular, in the normal course of action:
//   * do not alter existing id-values (altering names is fine)
//   * add new entries at the end, right before Count
//   * do not delete entries, mark them as deprecated instead
//   * treat altering semantics of existing mods as adding new (versions of) mods and deprecating the old ones
// * "garbage collection" may freely rearrange the list, must flush caches via TCommands::EngineTag() or suchlike
//

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
    Context_Deprecated, // input-only, preevaluated
    NoAutoSrc, // output-only, preevaluated
    NoRel, // output-only, preevaluated
    ResolveToBinDir, // input/output-only, preevaluated
    // latest additions TODO merge & sort
    ResourceUri,
    Requirements,
    Tared,
    OutputInclude,
    Hash,
    HideEmpty,
    Empty,
    Not,
    AddToIncl,
    DbgFail,
    Global,
    OutInclsFromInput,
    PrnOnlyRoot,
    Main,
    ParseBool,
    Comma,
    Result,
    InducedDeps,
    CutAllExt,
    NoTransformRelativeBuildDir,
    AddToModOutputs,
    DirAllowed,
    SkipByExtFilter,
    NoBuildRoot,
    //
    Count
};
static_assert(ToUnderlying(EMacroFunction::Count) <= (1 << NPolexpr::TFuncId::IDX_BITS));

template <>
struct std::formatter<EMacroFunction>: std::formatter<std::string_view> {
    auto format(EMacroFunction x, format_context& ctx) const {
        return formatter<string_view>::format(std::format("{}", static_cast<string_view>(ToString(x))), ctx);
    }
};

class TMacroValues {

public:

    // the "X" in "TXString[s]" is there mostly to increase visibility and greppability;
    // but one may also think that it stands for "eXpression"
    struct TXString {
        std::string Data;
        bool operator==(const TXString&) const = default;
    };
    struct TXStrings {
        std::vector<std::string> Data;
        bool operator==(const TXStrings&) const = default;
    };

    struct TTool {
        std::string Data;
        bool operator==(const TTool&) const = default;
    };
    struct TTools {
        std::vector<std::string> Data;
        bool operator==(const TTools&) const = default;
    };

    struct TResult {
        std::string Data;
        bool operator==(const TResult&) const = default;
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
    struct TOutputs {
        TVector<ui32> Coords;
        bool operator==(const TOutputs&) const = default;
    };

    struct TGlobPattern {
        TVector<std::string> Data;
        bool operator==(const TGlobPattern&) const = default;
    };
    struct TLegacyLateGlobPatterns {
        TVector<std::string> Data;
        bool operator==(const TLegacyLateGlobPatterns&) const = default;
    };

    using TValue = std::variant<
        std::monostate,
        bool,
        TXString,
        TXStrings,
        TTool, TTools,
        TResult,
        TInput, TInputs,
        TOutput, TOutputs,
        TGlobPattern,
        TLegacyLateGlobPatterns
    >;

public:

    class TInlineStorage {
    public:
        constexpr static size_t TypeBits = 2;
        constexpr static size_t DataBits = 26;
        static_assert(ToUnderlying(EDataType::CountInline) <= (1 << TypeBits));
        static_assert(TypeBits + DataBits == NPolexpr::TConstId::IDX_BITS);
    public:
        static NPolexpr::TConstId Put(EDataType type, auto data) {
            return NPolexpr::TConstId(ToUnderlying(EStorageType::Inline), (ToUnderlying(type) << DataBits) | data);
        }
        static std::pair<EDataType, ui32> Get(NPolexpr::TConstId id) {
            Y_ASSERT(id.GetStorage() == ToUnderlying(EStorageType::Inline));
            return {static_cast<EDataType>(id.GetIdx() >> DataBits), id.GetIdx() & ((1 << DataBits) - 1)};
        }
    };

public:

    NPolexpr::EVarId InsertVar(std::string_view name) { return static_cast<NPolexpr::EVarId>(Vars.Add(name)); }
    std::string_view GetVarName(NPolexpr::EVarId id) const;

    NPolexpr::TConstId InsertValue(const TValue& value);
    TValue GetValue(NPolexpr::TConstId id) const;
    TStringBuf Internalize(TStringBuf s);

    EDataType GetType(NPolexpr::TConstId id) const;

    void Save(TMultiBlobBuilder& builder) const;
    void Load(TBlob& multi);

private:

    TValueStorage Values;
    TNameStore Vars;

};
