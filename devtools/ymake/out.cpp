#include "out.h"

#include "ymake.h"
#include "macro.h"
#include "macro_vars.h"

#include <devtools/ymake/include_processors/include.h>

#include <util/generic/deque.h>
#include <util/string/join.h>

template <>
void Out<TVecDump>(IOutputStream& os, TTypeTraits<TVecDump>::TFuncParam v) {
    os << "vector of size " << v.V.size() << '\n';
    for (size_t i = 0; i < v.V.size(); ++i)
        os << "  " << i << ": " << v.V[i] << '\n';
}

template <>
void Out<TVecDumpSb>(IOutputStream& os, TTypeTraits<TVecDumpSb>::TFuncParam v) {
    os << "vector of size " << v.V.size() << '\n';
    for (size_t i = 0; i < v.V.size(); ++i)
        os << "  " << i << ": " << v.V[i] << '\n';
}

template <>
void Out<TSeqDump<TVector<TString>>>(IOutputStream& os, TTypeTraits<TSeqDump<TVector<TString>>>::TFuncParam v) {
    os << "vector of size " << v.V.size() << '\n';
    for (size_t i = 0; i < v.V.size(); ++i)
        os << "  " << i << ": " << v.V[i] << '\n';
}

template <>
void Out<TSeqDump<TVector<TStringBuf>>>(IOutputStream& os, TTypeTraits<TSeqDump<TVector<TStringBuf>>>::TFuncParam v) {
    os << "vector of size " << v.V.size() << '\n';
    for (size_t i = 0; i < v.V.size(); ++i)
        os << "  " << i << ": " << v.V[i] << '\n';
}

template <>
void Out<TSeqDump<TDeque<TStringBuf>>>(IOutputStream& os, TTypeTraits<TSeqDump<TDeque<TStringBuf>>>::TFuncParam v) {
    os << "deque of size " << v.V.size() << '\n';
    for (size_t i = 0; i < v.V.size(); ++i)
        os << "  " << i << ": " << v.V[i] << '\n';
}

template <>
void Out<TInclude>(IOutputStream& os, TTypeTraits<TInclude>::TFuncParam i) {
    os << (i.Kind == TInclude::EKind::System ? '<' : '"')
       << i.Path
       << (i.Kind == TInclude::EKind::System ? '>' : '"');
}

template <>
void Out<EMacroType>(IOutputStream& os, TTypeTraits<EMacroType>::TFuncParam i) {
    os << (i == EMT_Usual ? "Usual" : i == EMT_MacroDef ? "MacroDef" : i == EMT_MacroCall ? "MacroCall" : "WTF");
}

template <>
void Out<TVarStr>(IOutputStream& os, TTypeTraits<TVarStr>::TFuncParam var) {
    os << "{ flags=" << var.AllFlags << " name=" << var.Name << " }";
}

template <>
void Out<TVars>(IOutputStream& os, TTypeTraits<TVars>::TFuncParam vars) {
    os << "{id=" << vars.Id << " size=" << vars.size() << " vars = [ ";
    for (const auto& varsItem : vars) {
        os << varsItem.first << "=" << varsItem.second << " ";
    }

    os << "] ";
    if (vars.Base) {
        os << "base:" << "\n\t" << *vars.Base;
    }
    os << "}";
}

template <>
void Out<TYVar>(IOutputStream& os, TTypeTraits<TYVar>::TFuncParam vars) {
    os << "{ id=" << vars.Id << " vars=[ ";
    for (const auto& var : vars) {
        os << var << " ";
    }
    os << "] }";
}

template <>
void Out<const TYVar*>(IOutputStream& os, TTypeTraits<const TYVar*>::TFuncParam varPtr) {
    if (varPtr == nullptr) {
        os << "(nullptr)";
    } else {
        Out<TYVar>(os, *varPtr);
    }
}

template <>
void Out<TMacroData>(IOutputStream& os, TTypeTraits<TMacroData>::TFuncParam macroData) {
    os << "{ name=" << macroData.Name << " flags=" << macroData.AllFlags << " }";
}

template<>
void Out<std::reference_wrapper<const TAddDepDescr>>(IOutputStream& os, TTypeTraits<std::reference_wrapper<const TAddDepDescr>>::TFuncParam d) {
    os << "(" << Join(",", d.get().DepType, d.get().NodeType, d.get().ElemId) << ")";
}

template<>
void Out<const TModule*>(IOutputStream& os, TTypeTraits<const TModule*>::TFuncParam m) {
    os << static_cast<const void*>(m);
}
