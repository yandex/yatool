#pragma once

#include <devtools/ymake/symbols/file_store.h>

#include <util/generic/bitmap.h>
#include <util/generic/hash.h>
#include <util/generic/map.h>
#include <util/generic/vector.h>

#include <initializer_list>

enum ECmdFormat {
    ECF_Unset = 0,              // 'Ordinary' macro substitution
    ECF_Make = 1,               // Render command for command line
    ECF_Json = 2,               // Render command for JSON graph
    ECF_ExpandVars = 3,         // Just expand variables
    ECF_ExpandSimpleVars = 4,   // Similar to ECF_ExpandVars, but process and preserve some macro modifiers
                                // This format is intended for preprocessing of multi-commands before
                                // real rendering of command for command line or JSON graph
    ECF_ExpandFoldableVars = 5  // Similar to ECF_EXpandVars, but expand only foldable vars during FoldGlobalCommands
};

inline static bool NoMakeCommand(ECmdFormat format) {
    return format == ECF_Unset || format == ECF_ExpandVars || format == ECF_ExpandSimpleVars;
}

enum EMacroType {
    EMT_Usual = 0,
    EMT_MacroDef = 1,
    EMT_MacroCall = 2,
};

enum EModifierFlag {
    EMF_Unknown = 0,

    // modifiers
    EMF_Output,
    EMF_LateOut,
    EMF_Input,
    EMF_Tool,
    EMF_OutputInclude,
    EMF_Tmp,
    EMF_Prefix,
    EMF_Suffix,
    EMF_ExtFilter,
    EMF_AsStdout,
    EMF_WorkDir,
    EMF_SetEnv,
    EMF_CutExt,
    EMF_NoAutoSrc,                      // for outputs - do not automatically add as SRC
    EMF_Hide,                           // process (for input, output, etc., but do not print out)
    EMF_ResolveToBinDir,                // for outputs - write it to BINDIR
    EMF_NoTransformRelativeBuildDir,    // do not replace .. with __
    EMF_NoRel,                          // (for SRCS) output file dir. be strictly the same root-path as input's
    EMF_NoSpaceJoin,                    // stored in Prefix, used instead of spaces between multiple values
    EMF_PrnRootRel,                     // do not print abs. path but fixed path relative to arcadia root
    EMF_PrnOnlyRoot,                    // print abs. path of arcadia root (src dir or build dir)
    EMF_Quote,                          // quote in command, put as a single string in json
    EMF_BreakQuote,                     // like bash "$@"
    EMF_QuoteEach,                      // quote each word in a list
    EMF_WndBackSl,                      // replace forward slashes with backslashes (for windows paths, coords only)
    EMF_AddToIncl,                      // add file's directory to include search dirs (output only)
    EMF_HasDefaultExt,
    EMF_CutAllExt,
    EMF_CutPath,                        // only leave part after last slash '/'
    EMF_ToUpper,
    EMF_ToLower,
    EMF_DirAllowed,
    EMF_CommandSeparator,
    EMF_AddToModOutputs,
    EMF_KeyValue,
    EMF_Result,                         // only for inputs
    EMF_LastExt,                        // extract the last extention suffix from the file name
    EMF_Main,                           // whether it is main input/output or not
    EMF_Requirements,                   // requirements for node
    EMF_ModLocal,                       // localize path to a module binary dir
    EMF_Comma,                          // special modifier which is used to escape comma in args: ${comma:""}
    EMF_TagsIn,                         // a list of tags used to filter in PEERDIRs (makes sense only for PEERS internal var)
    EMF_TagsOut,                        // a list of tags used to filter out PEERDIRs (makes sense only for PEERS internal var)
    EMF_ResourceUri,                    // a list of resources' URIs used by command
    EMF_Context,                        // internal links context for file
    EMF_Global,                         // Mark input or output global. This makes entire command global like in SRCS(GLOBAL)
    EMF_TaredOut,                       // Mark output as tarred_out (will be unpacked in the nodes using it as input)
    EMF_HashVal,                        // Calculate md5 of the variable value
    EMF_InducedDeps,
    EMF_Namespace,                      // Set output to namespace: $B/path -> $B/namespace/path (used for package cmd)
    EMF_SkipByExt,
    EMF_OutInclsFromInput,              // Resolve output includes as inputs rather than includes
    // This element must be the last one
    EMF_ModifierFlagsSize
};

class TModifierFlags : public TBitMap<EMF_ModifierFlagsSize> {
    using TBase = TBitMap<EMF_ModifierFlagsSize>;
public:
    void Set(EModifierFlag flag) {
        TBase::Set(flag);
    }

    void Set(std::initializer_list<EModifierFlag> list) {
        for (auto modifier : list) {
            TBase::Set(modifier);
        }
    }

    bool Any(std::initializer_list<EModifierFlag> list) const {
        for (auto flag : list) {
            if (Get(flag)) {
                return true;
            }
        }
        return false;
    }

    bool All(std::initializer_list<EModifierFlag> list) const {
        for (auto flag : list) {
            if (!Get(flag)) {
                return false;
            }
        }
        return true;
    }
};

struct TMacro {
public:
    TMacro(const TStringBuf& name = TStringBuf())
        : AllFlags(0)
        , Name(name)
        , Context(ELinkType::ELT_Default)
        , FormatFor(ECF_Unset)
    {
    }

    TModifierFlags Flags;
    union {
        ui32 AllFlags;
        struct {
            ui32 ComplexArg : 1;        // $name(arg1, $arg2) - $arg2 is complex arg
            ui32 IsOwnArg : 1;
            ui32 HasArgs : 1;           // $name(arg1, arg2) - style
            ui32 HasOwnArgs : 1;        // =(arg1, arg2) - style
            ui32 SameName : 1;          // macro uses the same name as the analyzed string (i.e. `A=$A foo bar')
            ui32 RawString : 1;         // Name is some string, not a variable name
            ui32 Quoted : 1;            // within double-qotes, i.e. ..."$a"...
            ui32 EscInMod : 1;          // internal flag: need to remove back-slash escape symbol in mod fields
            ui32 CoordsFilled : 1;      // protect from double-add to DepIndex in some cases
            ui32 PrependQuoteDelim : 1;
        };
    };

    TStringBuf Name;
    TStringBuf Prefix; // or arguments when has arguments
    TStringBuf Namespace;
    TStringBuf Suffix;
    ELinkType Context;
    TStringBuf ExtFilter;
    TStringBuf SkipByExtFilter;
    TStringBuf NoSpaceJoin;
    ECmdFormat FormatFor;
    TVector<TVector<TStringBuf>> TagsIn;
    TVector<TVector<TStringBuf>> TagsOut;
    TStringBuf InducedDepsExt;

    void CopyFlags(const TMacro& from) {
        AllFlags = from.AllFlags;
    }

    void SetMod(const TStringBuf& name, const TStringBuf& val);

    TStringBuf& Args() {
        return Prefix;
    }

    const TStringBuf& Args() const {
        return Prefix;
    }

    bool NeedCoord() const {
        return Flags.Any({EMF_Input, EMF_Output, EMF_Tmp, EMF_Tool, EMF_OutputInclude, EMF_InducedDeps, EMF_Result});
    }

    TStringBuf CoordWhere() const {
        if (Flags.Get(EMF_Input)) {
            return TStringBuf("INPUT");
        } else if (Flags.Get(EMF_Output) || Flags.Get(EMF_Tmp)) {
            return TStringBuf("OUTPUT");
        } else if (Flags.Get(EMF_Tool)) {
            return TStringBuf("TOOL");
        } else {
            return TStringBuf("OUTPUT_INCLS");
        }
    }

    ECmdFormat GetFormatFor() const {
        return static_cast<ECmdFormat>(FormatFor);
    }
};

struct TMacroData: public TMacro {
    TStringBuf OrigFragment;
    ui32 DstStart;  // filled by SubstMacro
    ui32 DstEnd;    // filled by SubstMacro
};

const TStringBuf& ModifierFlagToName(EModifierFlag flag);
EModifierFlag ModifierNameToFlag(const TStringBuf& name);
