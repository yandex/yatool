#pragma once

#include "sem_graph.h"
#include "yexport_spec.h"

#include <util/generic/vector.h>

#include <string>
#include <vector>
#include <map>

struct TReplacement {
    TNodeSemantics Replacement;
    TNodeSemantics Addition;

    bool operator ==(const TReplacement& other) const = default;
};

using TPathView = std::string_view;
using TPathMap = std::map<TPathStr, int>;

struct TTargetReplacementsPrivateData {
    TVector<TReplacement> Replacements_; ///< All replaces, used by index from TPathMap
    TPathMap Replaces_; ///< Path prefixes for replacing Sem
    TPathMap Skips_; ///< Path prefixes for skip replacing Sem
    int GlobalReplacement_; ///< Global replacement (has only skip path prefixes) index if >= 0

    bool operator ==(const TTargetReplacementsPrivateData& other) const = default;
};

class TTargetReplacements : private TTargetReplacementsPrivateData {
public:

    using TPathMapIt = TPathMap::const_iterator;
    using TErrorCallback = std::function<void(TStringBuf error)>;

    TTargetReplacements()
        : TTargetReplacementsPrivateData{ .GlobalReplacement_ = -1 }
    {}

    /// Validate replacement spec before add
    bool ValidateSpec(const TTargetReplacementSpec& spec, const TErrorCallback& errorCallback) const;

    /// Add replacement spec without validate
    void AddSpec(const TTargetReplacementSpec& spec);

    /// Apply semantic replacement by path
    const TNodeSemantics& ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const;

    /// Return all internal data for tests
    const TTargetReplacementsPrivateData& GetPrivateData() const {
        return *this;
    }
private:
    mutable TNodeSemantics BufferSem_; ///< Buffer for semantic additions

    /// Validate replace path
    void ValidateReplace(const TPathStr& path, std::string& errors) const;

    /// Validate skip path
    void ValidateSkip(const TPathStr& path, std::string& errors) const;

    /// Add error text to list of errors
    static void AddError(std::string& errors, const TStringBuf error);

    /// Check path or some it parents for replacement
    const TReplacement* GetReplacement(TPathView path) const;

    /// Get inclusive iterators range from map by min and max path
    static std::tuple<TPathMapIt, TPathMapIt> GetRange(const TPathMap& map, TPathView minPath, TPathView maxPath);
};
