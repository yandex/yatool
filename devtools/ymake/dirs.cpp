#include "dirs.h"

void TDirs::IntoStrings(TVector<TString>& into) const {
    for (const auto& dir: *this) {
        into.emplace_back(dir.GetTargetStr());
    }
}

void TDirs::IntoStrings(TVector<TStringBuf>& into) const {
    for (const auto& dir: *this) {
        into.push_back(dir.GetTargetStr());
    }
}

void TDirs::RestoreFromsIds(const TVector<ui32>& ids, const TSymbols& names) {
    for (const auto& dirId : ids) {
        Push(names.FileNameById(dirId));
    }
}

TVector<ui32> TDirs::SaveAsIds() const {
    TVector<ui32> ids;
    ids.reserve(size());
    for (const auto& dir : *this) {
        ids.push_back(dir.GetElemId());
    }
    return ids;
}
