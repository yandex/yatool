#include "macro.h"

#include <devtools/ymake/diag/dbg.h>
#include <util/generic/algorithm.h>
#include <util/generic/hash.h>
#include <util/string/split.h>

// XXX: We suppose the only first character may be escape slash.
TStringBuf UndoEscapingJoinToken(const TStringBuf& name) {
    if (name.size() >= 2 && name.at(0) == '\\') {
        TStringBuf result = name;
        result.Skip(1);
        return result;
    }
    return name;
}

// FIXME(snermolaev): temporary fix unless constexpr-ness of std::pair constructors is clarified for MSVS
#undef STD_PAIR_CTOR_IS_CONSTEXPR
#if !defined(_MSC_VER) && (!defined(__GNUC__) || __GNUC__ > 4)
#define STD_PAIR_CTOR_IS_CONSTEXPR
#endif

namespace {

    using TElemType = std::pair<TStringBuf, EModifierFlag>;

#ifdef STD_PAIR_CTOR_IS_CONSTEXPR
    constexpr
#endif
    static const TElemType ModifierFlagsArray[] = {
        // "unknown" must be the 0-th element (see comments for ModifierFlagToModifierName
        // for additional details)
        { TStringBuf("unknown"), EMF_Unknown },

        { TStringBuf("output"), EMF_Output },
        { TStringBuf("late_out"), EMF_LateOut },
        { TStringBuf("input"), EMF_Input },
        { TStringBuf("tool"), EMF_Tool },
        { TStringBuf("output_include"), EMF_OutputInclude },
        { TStringBuf("tmp"), EMF_Tmp },
        { TStringBuf("pre"), EMF_Prefix },
        { TStringBuf("suf"), EMF_Suffix },
        { TStringBuf("ext"), EMF_ExtFilter },
        { TStringBuf("stdout"), EMF_AsStdout },
        { TStringBuf("cwd"), EMF_WorkDir },
        { TStringBuf("env"), EMF_SetEnv },
        { TStringBuf("noext"), EMF_CutExt },
        { TStringBuf("noauto"), EMF_NoAutoSrc },
        { TStringBuf("hide"), EMF_Hide },
        { TStringBuf("tobindir"), EMF_ResolveToBinDir },
        { TStringBuf("notransformbuilddir"), EMF_NoTransformRelativeBuildDir },
        { TStringBuf("norel"), EMF_NoRel },
        { TStringBuf("join"), EMF_NoSpaceJoin },
        { TStringBuf("rootrel"), EMF_PrnRootRel },
        { TStringBuf("rootdir"), EMF_PrnOnlyRoot },
        { TStringBuf("quo"), EMF_Quote },
        { TStringBuf("bq"), EMF_BreakQuote },
        { TStringBuf("qe"), EMF_QuoteEach },
        { TStringBuf("bksl"), EMF_WndBackSl },
        { TStringBuf("addincl"), EMF_AddToIncl },
        { TStringBuf("defext"), EMF_HasDefaultExt },
        { TStringBuf("noallext"), EMF_CutAllExt },
        { TStringBuf("nopath"), EMF_CutPath },
        { TStringBuf("toupper"), EMF_ToUpper },
        { TStringBuf("tolower"), EMF_ToLower },
        { TStringBuf("dirallowed"), EMF_DirAllowed },
        { TStringBuf("cmdsep"), EMF_CommandSeparator },
        { TStringBuf("add_to_outs"), EMF_AddToModOutputs },
        { TStringBuf("kv"), EMF_KeyValue },
        { TStringBuf("result"), EMF_Result },
        { TStringBuf("lastext"), EMF_LastExt },
        { TStringBuf("main"), EMF_Main },
        { TStringBuf("requirements"), EMF_Requirements },
        { TStringBuf("modlocal"), EMF_ModLocal },
        { TStringBuf("comma"), EMF_Comma },
        { TStringBuf("tags_in"), EMF_TagsIn },
        { TStringBuf("tags_out"), EMF_TagsOut },
        { TStringBuf("resource"), EMF_ResourceUri },
        { TStringBuf("context"), EMF_Context },
        { TStringBuf("global"), EMF_Global },
        { TStringBuf("tared"), EMF_TaredOut },
        { TStringBuf("hash"), EMF_HashVal },
        { TStringBuf("induced_deps"), EMF_InducedDeps },
        { TStringBuf("to_namespace"), EMF_Namespace },
        { TStringBuf("skip_by_ext"), EMF_SkipByExt },
        { TStringBuf("from_input"), EMF_OutInclsFromInput },

        { TStringBuf("<error>"), EMF_ModifierFlagsSize }
    };

#ifdef STD_PAIR_CTOR_IS_CONSTEXPR
    template <typename Type, size_t size>
    constexpr inline bool IsValidArray(const Type (&array)[size]) {
        size_t index = 0;
        for (const auto& elem : array) {
            if (elem.second != index++) {
                return false;
            }
        }
        return true;
    }
#endif

    static const THashMap<TStringBuf, EModifierFlag> NameToFlagMap(std::begin(ModifierFlagsArray), std::end(ModifierFlagsArray));

    void UpdateMacroTags(TStringBuf value, TVector<TVector<TStringBuf>>& tags) {
        for (const auto& it : StringSplitter(value).Split('|').SkipEmpty()) {
            TVector<TStringBuf> subTags = StringSplitter(it.Token()).Split(',').SkipEmpty();
            if (!subTags.empty()) {
                tags.push_back(std::move(subTags));
            }
        }
    }

} // anonymous namespace

EModifierFlag ModifierNameToFlag(const TStringBuf& name) {
    auto it = NameToFlagMap.find(name);
    return it == NameToFlagMap.end() ? EMF_Unknown : it->second;
}

const TStringBuf& ModifierFlagToName(EModifierFlag flag) {
    // FIXME(snermolaev): temporary fix unless constexpr-ness of std::pair constructors is clarified for MSVS
#ifdef STD_PAIR_CTOR_IS_CONSTEXPR
    // Check if the array ModifierFlagsArray is filled appropriately because this function
    // counts on the order of elements
    static_assert(IsValidArray(ModifierFlagsArray), "ModifierFlagsArray is not properly set");
#endif
    Y_ASSERT(flag >= EMF_Unknown && flag < EMF_ModifierFlagsSize);
    flag = (flag < EMF_Unknown || flag > EMF_ModifierFlagsSize) ? EMF_Unknown : flag;
    return ModifierFlagsArray[flag].first;
}

void TMacro::SetMod(const TStringBuf& name, const TStringBuf& val) {
    EModifierFlag modifier = ModifierNameToFlag(name);
    switch (modifier) {
        case EMF_Unknown:
            YDIAG(V) << "Unknown mod: " << name << Endl;
            break;
        case EMF_Prefix:
            Prefix = val;
            break;
        case EMF_Namespace:
            Namespace = val;
            break;
        case EMF_Suffix:
            Suffix = val;
            break;
        case EMF_Context:
            Context = TFileConf::GetContextType(val);
            break;
        case EMF_NoSpaceJoin:
            NoSpaceJoin = UndoEscapingJoinToken(val);
            break;
        case EMF_ExtFilter:
            ExtFilter = val;
            break;
        case EMF_SkipByExt:
            SkipByExtFilter = val;
            break;
        case EMF_HasDefaultExt:
            Suffix = val;
            break;
        case EMF_ToUpper:
            if (Flags.Get(EMF_ToLower)) {
                return;
            }
            break;
        case EMF_ToLower:
            if (Flags.Get(EMF_ToUpper)) {
                return;
            }
            break;
        case EMF_TagsIn:
            UpdateMacroTags(val, TagsIn);
            break;
        case EMF_TagsOut:
            UpdateMacroTags(val, TagsOut);
            break;
        case EMF_InducedDeps:
            if (!InducedDepsExt.empty()) {
                ythrow TMakeError() << "Multiple induced_deps are not allowed";
            }
            InducedDepsExt = val;
            break;
        default:
            // Do nothing
            ;
    }
    Flags.Set(modifier);
}
