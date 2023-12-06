#include "target_replacements.h"

#include <util/string/split.h>
#include <util/str_stl.h>

namespace NYexport {

static constexpr const char* SEPARATORS = "/";

/// Validate replacement spec before add
bool TTargetReplacements::ValidateSpec(const TTargetReplacementSpec& spec, const TErrorCallback& errorCallback) const {
    std::string errors;
    if (!spec.SkipPathPrefixes.empty()) {
        if (GlobalReplacement_ >= 0) {
            AddError(errors, "Few global replacements, must be only one global replacement (without replace path prefixes)");
        }
    }
    for (const auto& pathPrefix: spec.ReplacePathPrefixes) {
        ValidateReplace(pathPrefix.Path, errors);
        for (const auto& except : pathPrefix.Excepts) {
            ValidateSkip(except, errors);
        }
    }
    for (const auto& pathPrefix: spec.SkipPathPrefixes) {
        ValidateSkip(pathPrefix.Path, errors);
        for (const auto& except : pathPrefix.Excepts) {
            ValidateReplace(except, errors);
        }
    }
    if (!errors.empty()) {
        errorCallback(errors);
        return false;
    }
    return true;
}

/// Add replacement spec without validate
void TTargetReplacements::AddSpec(const TTargetReplacementSpec& spec) {
    auto iReplacement= Replacements_.size();
    if (!spec.SkipPathPrefixes.empty()) {
        Y_ASSERT(GlobalReplacement_ < 0);
        GlobalReplacement_ = iReplacement;
    }
    Replacements_.emplace_back(TReplacement{ .Replacement = spec.Replacement, .Addition = spec.Addition });
    for (const auto& pathPrefix: spec.ReplacePathPrefixes) {
        Replaces_.insert_or_assign(pathPrefix.Path, iReplacement);
        for (const auto& except : pathPrefix.Excepts) {
            Skips_.insert_or_assign(except, iReplacement);
        }
    }
    for (const auto& pathPrefix: spec.SkipPathPrefixes) {
        Skips_.insert_or_assign(pathPrefix.Path, iReplacement);
        for (const auto& except : pathPrefix.Excepts) {
            Replaces_.insert_or_assign(except, iReplacement);
        }
    }
}

/// Apply Sem replacement by path
const TNodeSemantics& TTargetReplacements::ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const {
    const auto* replacement = GetReplacement(path);
    if (!replacement) {
        return inputSem;
    }
    if (!replacement->Replacement.empty()) {
        return replacement->Replacement;
    }
    BufferSem_ = inputSem;
    Y_ASSERT(!replacement->Addition.empty());
    BufferSem_.insert(BufferSem_.end(), replacement->Addition.begin(), replacement->Addition.end());
    return BufferSem_;
}

/// Validate replace path
void TTargetReplacements::ValidateReplace(const TPathStr& path, std::string& errors) const {
    if (Skips_.find(path) != Skips_.end()) {
        AddError(errors, "Try replace skiped path prefix " + path);
    }
    if (Replaces_.find(path) != Replaces_.end()) {
        AddError(errors, "Repeated replace path prefix " + path);
    }
}

/// Validate skip path
void TTargetReplacements::ValidateSkip(const TPathStr& path, std::string& errors) const {
    if (Replaces_.find(path) != Replaces_.end()) {
        AddError(errors, "Try skip replaced path prefix " + path);
    }
    if (Skips_.find(path) != Skips_.end()) {
        AddError(errors, "Repeated skip path prefix " + path);
    }
}

/// Add error text to list of errors
void TTargetReplacements::AddError(std::string& errors, const TStringBuf error) {
    errors += (errors.empty() ? "; " : "");
    errors += error;
}

/// Check path or some it parents for replacement
const TReplacement* TTargetReplacements::GetReplacement(TPathView path) const {
    Y_ASSERT(!path.empty());
    auto hasReplaces = !Replaces_.empty();
    auto hasSkips = !Skips_.empty();
    if (!hasReplaces && !hasSkips) {
        return nullptr;
    }

    if (NPath::IsTypedPath(path)) {
        path = NPath::CutType(path);
    }

    auto firstSeparator = path.find_first_of(SEPARATORS);
    auto minPath = firstSeparator == TPathStr::npos
        ? path
        : TPathView(path.data(), firstSeparator);
    TPathMapIt minReplaces;
    TPathMapIt maxReplaces;
    TPathMapIt minSkips;
    TPathMapIt maxSkips;
    if (hasReplaces) {
        const auto [ min, max ] = GetRange(Replaces_, minPath, path);
        if (min == Replaces_.end()) {
            hasReplaces = false;
        } else {
            minReplaces = min;
            maxReplaces = max;
        }
    }
    if (hasSkips) {
        const auto [ min, max ] = GetRange(Skips_, minPath, path);
        if (min == Skips_.end()) {
            hasSkips = false;
        } else {
            minSkips = min;
            maxSkips = max;
        }
    }
    while (hasReplaces || hasSkips) {
        if (hasReplaces) {
            while (maxReplaces->first >= path) {
                if (maxReplaces->first == path) {
                    return &Replacements_[maxReplaces->second];
                }
                if (maxReplaces == minReplaces) {
                    hasReplaces = false;
                    break;
                } else {
                    --maxReplaces;
                }
            }
        }
        if (hasSkips) {
            while (maxSkips->first >= path) {
                if (maxSkips->first == path) {
                    return nullptr;
                }
                if (maxSkips == minSkips) {
                    hasSkips = false;
                    break;
                } else {
                    --maxSkips;
                }
            }
        }
        auto lastSeparator = path.find_last_of(SEPARATORS);
        if (lastSeparator == TPathStr::npos) {
            break;
        }
        path = TPathView(path.data(), lastSeparator);
    }
    if (GlobalReplacement_ >= 0) { // apply global replacement for path
        return &Replacements_[GlobalReplacement_];
    }
    return nullptr;
}

/// Get inclusive iterators range from map by min and max path
std::tuple<TTargetReplacements::TPathMapIt, TTargetReplacements::TPathMapIt> TTargetReplacements::GetRange(const TPathMap& map, TPathView minPath, TPathView maxPath) {
    auto max = map.lower_bound(TPathStr{maxPath});
    if (max == map.end()) {
        --max;
    }
    TPathMap::const_iterator min;
    if (minPath.size() == maxPath.size()) {
        min = max;
    } else {
        min = map.lower_bound(TPathStr{minPath});
    }
    return { min, max };
}

}
