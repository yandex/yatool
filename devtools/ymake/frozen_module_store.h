#pragma once

#include "module_state.h"
#include "vardefs.h"

#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/memory/blob.h>
#include <util/ysaveload.h>

#include <array>
#include <functional>
#include <iterator>
#include <limits>

class IInputStream;
class IOutputStream;
class TDepGraph;
class TModules;

namespace NFrozenModuleStore {
    struct THeader {
        ui64 ModuleCount = 0;
        ui64 GroupCount = 0;

        Y_SAVELOAD_DEFINE(ModuleCount, GroupCount);
    };

    struct TModuleDescriptor {
        ui32 ModuleId = 0;
        ui32 MakefileId = 0;
        ui32 GroupIndex = 0;
        ui64 RecordOffset = 0;
        ui64 RecordSize = 0;
        ui64 DMOffset = 0;
        ui64 DMSize = 0;

        Y_SAVELOAD_DEFINE(ModuleId, MakefileId, GroupIndex, RecordOffset, RecordSize, DMOffset, DMSize);
    };

    struct TGroupDescriptor {
        ui32 MakefileId = 0;
        ui64 FirstMember = 0;
        ui64 MemberCount = 0;

        Y_SAVELOAD_DEFINE(MakefileId, FirstMember, MemberCount);
    };
}

/// Keeps cached modules serialized and materializes them lazily by makefile group.
class TFrozenModuleStore {
public:
    explicit TFrozenModuleStore(TModules& owner)
        : Owner(owner)
    {
    }

    /// Verify every record using temporary saved states, then attach the image.
    /// Does not construct cached TModule objects.
    void Load(const TBlob& blob);

    /// Save the logical module set, reusing untouched frozen records.
    void Save(IOutputStream* output);

    /// Clear logical runtime state without materializing the frozen records.
    void ClearRawIncludes() {
        RawIncludesCleared = true;
    }

    /// Check logical membership, excluding suppressed frozen modules.
    bool Contains(TFileElemId id) const {
        return !SuppressedModules.contains(id) && HasDescriptor(id);
    }

    /// Check whether the loaded image has a descriptor, including suppressed modules.
    bool HasDescriptor(TFileElemId id) const {
        return FindDescriptor(id) != NoIndex;
    }

    /// Hide a frozen module superseded or invalidated by the current run.
    void Suppress(TFileElemId id) {
        SuppressedModules.insert(id);
    }

    /// Materialization always loads the whole makefile group.
    void MaterializeModule(TFileElemId id);
    void MaterializeMakefile(TFileElemId makefileId);

    /// Load dependency management data without materializing frozen modules.
    /// Symbols and graph must already be loaded.
    void LoadDMCache(IInputStream* input, const TDepGraph& graph);

    /// Save dependency management data after the algorithm has completed.
    void SaveDMCache(IOutputStream* output, const TDepGraph& graph);

    void AccumulateStats(ui64& total, ui64& loaded, ui64& outdated) const;

    size_t ModuleCount() const {
        return Header.ModuleCount;
    }
    void Clear() {
        TFrozenModuleStore empty{Owner.get()};
        std::swap(*this, empty);
    }

private:
    // The order is part of the serialized TDMData format.
    inline static constexpr TStringBuf DM_VAR_NAMES[] = {
        NVariableDefs::VAR_NON_NAMAGEABLE_PEERS,
        NVariableDefs::VAR_DART_CLASSPATH_DEPS,
        NVariableDefs::VAR_MANAGED_PEERS,
        NVariableDefs::VAR_MANAGED_PEERS_CLOSURE,
        NVariableDefs::VAR_DART_CLASSPATH,
        NVariableDefs::VAR_UNITTEST_MOD,
        NVariableDefs::VAR_DEPENDENCY_MANAGEMENT_TAGS_EXCLUDE,
        NVariableDefs::VAR_DEPENDENCY_MANAGEMENT_TRANSPARENT,
    };
    static constexpr size_t DM_VAR_COUNT = std::size(DM_VAR_NAMES);

    static constexpr ui64 NoIndex = std::numeric_limits<ui64>::max();

    class TSaveImageBuilder;

    struct TDMData {
        TVector<TString> Vars;
        bool PeersComplete = false;

        Y_SAVELOAD_DEFINE(Vars, PeersComplete);
    };

    struct TDMCacheRecord {
        TVector<TElemId> UniqPeersIds;
        TVector<TElemId> ManagedDirectPeersIds;
        THashMap<TString, TString> Vars;
        bool PeersComplete = false;

        Y_SAVELOAD_DEFINE(UniqPeersIds, ManagedDirectPeersIds, Vars, PeersComplete);
    };

    struct TPendingDMData {
        TFileElemId ModuleId;
        std::array<ui32, DM_VAR_COUNT> VarOffsets{};
        bool PeersComplete = false;
    };

    NFrozenModuleStore::TModuleDescriptor Descriptor(ui64 index) const;
    NFrozenModuleStore::TGroupDescriptor Group(ui64 index) const;
    ui32 GroupMember(ui64 index) const;
    ui64 FindDescriptor(TFileElemId id) const;
    ui64 FindGroup(TFileElemId makefileId) const;
    const TPendingDMData* FindPendingDMData(TFileElemId id) const;
    void LoadImage(const TBlob& blob);
    void ValidateImage() const;
    TModuleSavedState LoadModuleState(const NFrozenModuleStore::TModuleDescriptor& descriptor) const;
    void MaterializeGroup(ui64 groupIndex);

    void ApplyPendingDMData(TModule& module);
    TStringBuf PendingDMValue(const TPendingDMData& data, size_t index) const;
    TDMData ExpandPendingDMData(const TPendingDMData& data) const;
    TDMData ReloadDMData(const TVector<TCmdElemId>& configVars, TFileElemId makefileId) const;
    TDMData LoadFrozenDMData(ui64 descriptorIndex) const;
    TVector<TFileElemId> LogicalModuleIds() const;
    const char* PayloadData(ui64 offset, ui64 size) const;

private:
    std::reference_wrapper<TModules> Owner;

    TBlob Image;
    NFrozenModuleStore::THeader Header;
    ui64 DescriptorsOffset = 0;
    ui64 GroupsOffset = 0;
    ui64 MembersOffset = 0;
    ui64 PayloadOffset = 0;
    bool RawIncludesCleared = false;
    THashSet<TFileElemId> SuppressedModules;
    TVector<TPendingDMData> PendingDMData;
    TString PendingDMStrings;
};
