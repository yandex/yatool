#pragma once

#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/symbols/name_store.h>
#include <devtools/ymake/symbols/symbols.h>

#include <util/generic/strbuf.h>
#include <util/generic/string.h>


template <>
class TUniqVector<TFileView, false> : public TUniqContainerImpl<TFileView, TFileView, 32> {};

class TDirs : public TUniqVector<TFileView>, TMoveOnly {
public:
    void IntoStrings(TVector<TString>& into) const;
    void IntoStrings(TVector<TStringBuf>& into) const;

    TVector<ui32> SaveAsIds() const;
    void RestoreFromsIds(const TVector<ui32>& ids, const TSymbols& names);
};