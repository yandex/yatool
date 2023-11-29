#pragma once

#include <util/generic/string.h>
#include <util/string/cast.h>

struct TResourceSectionParams {
    bool Untar : 1;
    bool ToolDir : 1;
};

inline bool operator==(TResourceSectionParams a, TResourceSectionParams b) {
    return a.Untar == b.Untar && a.ToolDir == b.ToolDir;
}

inline bool operator!=(TResourceSectionParams a, TResourceSectionParams b) {
    return !operator==(a, b);
}

template<>
inline TString ToString<TResourceSectionParams>(const TResourceSectionParams &a) {
    return TString(a.Untar ? "untar" : "nountar") + (a.ToolDir ? ", tool" : ", build");
}

template<typename Other>
inline bool operator==(const std::pair<TString, Other>& a, const std::pair<TStringBuf, Other>& b) {
    return a.first == b.first && a.second == b.second;
}

template<typename Other>
inline bool operator!=(const std::pair<TString, Other>& a, const std::pair<TStringBuf, Other>& b) {
    return !operator==(a, b);
}
