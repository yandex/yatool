#include "frozen_module_store.h"

#include "dependency_management.h"
#include "json_saveload.h"
#include "module_store.h"

#include <library/cpp/iterator/enumerate.h>

#include <util/generic/algorithm.h>
#include <util/generic/overloaded.h>
#include <util/generic/xrange.h>
#include <util/generic/yexception.h>
#include <util/stream/length.h>
#include <util/stream/str.h>

#include <limits>
#include <tuple>
#include <utility>
#include <variant>

// Frozen image layout:
//   THeader
//   TModuleDescriptor[ModuleCount]        sorted by ModuleId
//   TGroupDescriptor[GroupCount]          sorted by MakefileId
//   ui32 group members[ModuleCount]       indexes into module descriptors
//   serialized TModuleSavedState records
//   serialized TDMData records
// Record and DM offsets are relative to the payload formed by the last two sections.

namespace {
    template <typename T>
    size_t SerializedSize(const T& value) {
        TNullOutput nullOutput;
        TCountingOutput output(&nullOutput);
        ::Save(&output, value);
        return output.Counter();
    }

    template <typename T>
    std::pair<ui64, ui64> AppendSerialized(TString& output, const T& value) {
        const ui64 offset = output.size();
        TStringOutput stream(output);
        ::Save(&stream, value);
        stream.Finish();
        return {offset, output.size() - offset};
    }
}

class TFrozenModuleStore::TSaveImageBuilder {
private:
    struct TFrozenSource {
        ui64 DescriptorIndex;
    };

    using TSaveSource = std::variant<std::reference_wrapper<TModule>, TFrozenSource>;

    struct TSaveEntry {
        ui32 ModuleId;
        ui32 MakefileId;
        TSaveSource Source;
    };

public:
    explicit TSaveImageBuilder(const TFrozenModuleStore& store)
        : Store(store)
    {
    }

    void Write(IOutputStream* output);

private:
    void CollectEntries();
    void SerializeEntries();
    void BuildGroups();

private:
    const TFrozenModuleStore& Store;
    TVector<TSaveEntry> Entries;
    TVector<NFrozenModuleStore::TModuleDescriptor> Descriptors;
    TVector<NFrozenModuleStore::TGroupDescriptor> Groups;
    TVector<ui32> Members;
    TString Records;
    TString DMPayload;
};

void TFrozenModuleStore::TSaveImageBuilder::CollectEntries() {
    Entries.reserve(Store.Header.ModuleCount + Store.Owner.get().ModulesById.size());
    for (ui64 i = 0; i < Store.Header.ModuleCount; ++i) {
        const auto descriptor = Store.Descriptor(i);
        const TFileElemId id(descriptor.ModuleId);
        if (Store.Owner.get().ModulesById.contains(id) || Store.SuppressedModules.contains(id) || Store.Owner.get().ReparsedMakefiles.contains(TFileElemId(descriptor.MakefileId))) {
            continue;
        }
        Entries.push_back({descriptor.ModuleId, descriptor.MakefileId, TFrozenSource{i}});
    }
    for (const auto [id, module] : Store.Owner.get().ModulesById) {
        if (Store.Owner.get().Loaded && module->IsLoaded() && Store.Owner.get().ReparsedMakefiles.contains(module->GetMakefileId())) {
            YDIAG(VV) << "Ignore outdated module: " << module->GetName() << Endl;
            continue;
        }
        Entries.push_back({RawElemId(id), RawElemId(module->GetMakefileId()), std::ref(*module)});
    }
    SortBy(Entries, [](const TSaveEntry& entry) { return entry.ModuleId; });
    Y_ENSURE(Entries.size() < std::numeric_limits<ui32>::max(), "Too many modules to save");
    for (size_t i = 1; i < Entries.size(); ++i) {
        Y_ENSURE(Entries[i - 1].ModuleId != Entries[i].ModuleId, "Duplicate module while saving modules image");
    }
}

void TFrozenModuleStore::TSaveImageBuilder::SerializeEntries() {
    Descriptors.reserve(Entries.size());
    for (const auto& entry : Entries) {
        NFrozenModuleStore::TModuleDescriptor descriptor;
        descriptor.ModuleId = entry.ModuleId;
        descriptor.MakefileId = entry.MakefileId;
        std::visit(TOverloaded{
            [&](std::reference_wrapper<TModule> moduleRef) {
                TModule& module = moduleRef.get();
                std::tie(descriptor.RecordOffset, descriptor.RecordSize) = AppendSerialized(Records, TModuleSavedState(module));

                // The main cache restores configure state, not a previous DM
                // calculation. Match TModule(saved), including incomplete peers.
                std::tie(descriptor.DMOffset, descriptor.DMSize) = AppendSerialized(DMPayload, Store.ReloadDMData(module.ConfigVars, module.GetMakefileId()));
            },
            [&](TFrozenSource source) {
                const auto frozen = Store.Descriptor(source.DescriptorIndex);
                if (Store.RawIncludesCleared) {
                    // A previous clear changed the logical record. Re-encode
                    // that state without constructing or committing a TModule.
                    std::tie(descriptor.RecordOffset, descriptor.RecordSize) = AppendSerialized(Records, Store.LoadModuleState(frozen));
                } else {
                    descriptor.RecordOffset = Records.size();
                    descriptor.RecordSize = frozen.RecordSize;
                    Records.append(Store.PayloadData(frozen.RecordOffset, frozen.RecordSize), frozen.RecordSize);
                }

                // Pending DM belongs to the separate DM cache. It must not
                // replace the configure-state defaults on a main-cache reload.
                descriptor.DMOffset = DMPayload.size();
                descriptor.DMSize = frozen.DMSize;
                DMPayload.append(Store.PayloadData(frozen.DMOffset, frozen.DMSize), frozen.DMSize);
            },
        }, entry.Source);
        Descriptors.push_back(descriptor);
    }

    for (auto& descriptor : Descriptors) {
        descriptor.DMOffset += Records.size();
    }
}

void TFrozenModuleStore::TSaveImageBuilder::BuildGroups() {
    TVector<std::pair<ui32, ui32>> groupedMembers;
    groupedMembers.reserve(Descriptors.size());
    for (size_t i = 0; i < Descriptors.size(); ++i) {
        groupedMembers.emplace_back(Descriptors[i].MakefileId, static_cast<ui32>(i));
    }
    Sort(groupedMembers);

    Members.reserve(groupedMembers.size());
    for (size_t i = 0; i < groupedMembers.size();) {
        const ui32 makefileId = groupedMembers[i].first;
        const ui64 firstMember = Members.size();
        while (i < groupedMembers.size() && groupedMembers[i].first == makefileId) {
            const ui32 descriptorIndex = groupedMembers[i++].second;
            Descriptors[descriptorIndex].GroupIndex = Groups.size();
            Members.push_back(descriptorIndex);
        }
        Groups.push_back({makefileId, firstMember, Members.size() - firstMember});
    }
}

void TFrozenModuleStore::TSaveImageBuilder::Write(IOutputStream* output) {
    CollectEntries();
    SerializeEntries();
    BuildGroups();

    NFrozenModuleStore::THeader header;
    header.ModuleCount = Descriptors.size();
    header.GroupCount = Groups.size();
    Y_ASSERT(header.ModuleCount == Members.size());

    ::Save(output, header);
    ApplyToMany([&](const auto& items) {
        ::SaveRange(output, items.begin(), items.end());
    }, Descriptors, Groups, Members);
    output->Write(Records.data(), Records.size());
    // Release the copied records before the output grows for the DM payload.
    TString{}.swap(Records);
    output->Write(DMPayload.data(), DMPayload.size());
}

void TFrozenModuleStore::Load(const TBlob& blob) {
    // Do not expose even the index of an image whose records failed validation.
    TFrozenModuleStore candidate{Owner.get()};
    candidate.LoadImage(blob);
    candidate.ValidateImage();
    std::swap(*this, candidate);

    YDIAG(V) << "Loaded frozen modules image: " << Header.ModuleCount << " modules in "
             << Header.GroupCount << " makefile groups, " << Image.Size() - PayloadOffset << " payload bytes" << Endl;
}

void TFrozenModuleStore::LoadImage(const TBlob& blob) {
    const size_t headerSize = SerializedSize(NFrozenModuleStore::THeader{});
    Y_ENSURE(blob.Size() >= headerSize, "Truncated modules image header");

    NFrozenModuleStore::THeader header;
    TMemoryInput headerInput(blob.Data(), headerSize);
    ::Load(&headerInput, header);
    Y_ENSURE(headerInput.Exhausted(), "Invalid modules image header size");

    ui64 offset = headerSize;
    const auto consume = [&](ui64 count, ui64 itemSize, TStringBuf section) {
        Y_ENSURE(count <= (blob.Size() - offset) / itemSize, "Truncated modules image " << section);
        offset += count * itemSize;
    };

    const ui64 descriptorsOffset = offset;
    consume(header.ModuleCount, SerializedSize(NFrozenModuleStore::TModuleDescriptor{}), "descriptors");
    const ui64 groupsOffset = offset;
    consume(header.GroupCount, SerializedSize(NFrozenModuleStore::TGroupDescriptor{}), "groups");
    const ui64 membersOffset = offset;
    consume(header.ModuleCount, SerializedSize(ui32{}), "members");

    Image = blob;
    Header = header;
    DescriptorsOffset = descriptorsOffset;
    GroupsOffset = groupsOffset;
    MembersOffset = membersOffset;
    PayloadOffset = offset;
}

void TFrozenModuleStore::ValidateImage() const {
    Y_ENSURE(Header.GroupCount <= Header.ModuleCount, "Too many frozen module groups");
    ui32 previousModuleId = 0;
    for (ui64 i = 0; i < Header.ModuleCount; ++i) {
        const auto descriptor = Descriptor(i);
        Y_ENSURE(i == 0 || previousModuleId < descriptor.ModuleId, "Frozen module IDs are not strictly increasing");
        previousModuleId = descriptor.ModuleId;
        Y_ENSURE(descriptor.GroupIndex < Header.GroupCount, "Frozen module group index is out of bounds");

        // Use the actual deserializers, retaining at most one temporary record.
        // All format failures occur inside TYMake::Load's existing recovery.
        (void)LoadModuleState(descriptor);
        (void)LoadFrozenDMData(i);
    }

    ui64 nextMember = 0;
    ui32 previousMakefileId = 0;
    for (ui64 i = 0; i < Header.GroupCount; ++i) {
        const auto group = Group(i);
        Y_ENSURE(i == 0 || previousMakefileId < group.MakefileId, "Frozen makefile IDs are not strictly increasing");
        previousMakefileId = group.MakefileId;
        Y_ENSURE(group.FirstMember == nextMember && group.MemberCount != 0 && group.MemberCount <= Header.ModuleCount - nextMember,
                 "Invalid frozen module group member range");
        ui32 previousMember = 0;
        for (ui64 j = 0; j < group.MemberCount; ++j) {
            const auto member = GroupMember(nextMember + j);
            Y_ENSURE(j == 0 || previousMember < member, "Frozen module group members are not strictly increasing");
            previousMember = member;
            const auto descriptor = Descriptor(member);
            Y_ENSURE(descriptor.GroupIndex == i && descriptor.MakefileId == group.MakefileId,
                     "Frozen module descriptor does not match its group");
        }
        nextMember += group.MemberCount;
    }
    // Ranges partition the member table; strict order excludes duplicates in
    // each group and GroupIndex excludes duplicates across different groups.
    Y_ENSURE(nextMember == Header.ModuleCount, "Incomplete frozen module group membership");
}

TModuleSavedState TFrozenModuleStore::LoadModuleState(const NFrozenModuleStore::TModuleDescriptor& descriptor) const {
    TMemoryInput input(PayloadData(descriptor.RecordOffset, descriptor.RecordSize), descriptor.RecordSize);
    TModuleSavedState state;
    ::Load(&input, state);
    Y_ENSURE(input.Exhausted(), "Trailing data in frozen module record");
    Y_ENSURE(RawElemId(state.Id) == descriptor.ModuleId && RawElemId(state.MakefileId) == descriptor.MakefileId,
             "Frozen module descriptor does not match its record");
    if (RawIncludesCleared) {
        state.RawIncludes.clear();
    }
    return state;
}

void TFrozenModuleStore::LoadDMCache(IInputStream* input, const TDepGraph& graph) {
    PendingDMData.clear();
    PendingDMStrings.clear();
    // Offset zero denotes an absent value.
    AppendSerialized(PendingDMStrings, ui32{});

    const auto logicalIds = LogicalModuleIds();
    const ui32 modulesCount = LoadFromStream<ui32>(input);
    Y_ENSURE(modulesCount == logicalIds.size(), "Dependency management cache has an incomplete module set");
    PendingDMData.reserve(modulesCount);
    THashSet<TFileElemId> loadedIds;
    loadedIds.reserve(modulesCount);
    for (ui32 i = 0; i < modulesCount; ++i) {
        const TFileElemId modId(LoadFromStream<ui32>(input));
        Y_ENSURE(Owner.get().ContainsUnlocked(modId), "Dependency management cache refers to an unknown module " << RawElemId(modId));
        Y_ENSURE(loadedIds.insert(modId).second, "Dependency management cache contains a duplicate module " << RawElemId(modId));

        TDMCacheRecord record;
        ::Load(input, record);
        auto& moduleLists = Owner.get().GetModuleNodeIds(modId);
        for (auto peer : record.UniqPeersIds) {
            const TNodeId nodeId = graph.GetFileNode(Owner.get().Symbols.FileConf.GetName(AssumeFile(peer))).Id();
            Owner.get().GetNodeListStore().AddToList(moduleLists.UniqPeers, nodeId);
        }
        for (auto peer : record.ManagedDirectPeersIds) {
            const TNodeId nodeId = graph.GetFileNode(Owner.get().Symbols.FileConf.GetName(AssumeFile(peer))).Id();
            Owner.get().GetNodeListStore().AddToList(moduleLists.ManagedDirectPeers, nodeId);
        }

        TPendingDMData pending;
        pending.ModuleId = modId;
        pending.PeersComplete = record.PeersComplete;
        for (const auto [varIndex, varName] : Enumerate(DM_VAR_NAMES)) {
            const auto it = record.Vars.find(varName);
            if (it == record.Vars.end() || it->second.empty()) {
                continue;
            }
            const auto& value = it->second;
            Y_ENSURE(value.size() <= std::numeric_limits<ui32>::max(), "Dependency management variable is too large");
            Y_ENSURE(PendingDMStrings.size() <= std::numeric_limits<ui32>::max() && value.size() + sizeof(ui32) <= std::numeric_limits<ui32>::max() - PendingDMStrings.size(), "Dependency management string arena is too large");
            pending.VarOffsets[varIndex] = PendingDMStrings.size();
            AppendSerialized(PendingDMStrings, static_cast<ui32>(value.size()));
            PendingDMStrings.append(value);
        }
        PendingDMData.push_back(pending);
    }
    SortBy(PendingDMData, [](const TPendingDMData& data) { return data.ModuleId; });
    for (const auto& [id, module] : Owner.get().ModulesById) {
        ApplyPendingDMData(*module);
    }
}

void TFrozenModuleStore::MaterializeModule(TFileElemId id) {
    const ui64 descriptorIndex = FindDescriptor(id);
    if (descriptorIndex == NoIndex || SuppressedModules.contains(id)) {
        return;
    }
    MaterializeGroup(Descriptor(descriptorIndex).GroupIndex);
}

void TFrozenModuleStore::MaterializeMakefile(TFileElemId makefileId) {
    if (const ui64 groupIndex = FindGroup(makefileId); groupIndex != NoIndex) {
        MaterializeGroup(groupIndex);
    }
}

void TFrozenModuleStore::MaterializeGroup(ui64 groupIndex) {
    const auto group = Group(groupIndex);
    TVector<TModuleSavedState> states;
    states.reserve(group.MemberCount);
    for (ui64 i = 0; i < group.MemberCount; ++i) {
        const auto descriptor = Descriptor(GroupMember(group.FirstMember + i));
        const TFileElemId id(descriptor.ModuleId);
        if (Owner.get().ModulesById.contains(id) || SuppressedModules.contains(id)) {
            continue;
        }

        states.push_back(LoadModuleState(descriptor));
    }

    ui64 materialized = 0;
    for (auto& state : states) {
        auto* module = new TModule(std::move(state), Owner.get().CreationContext);
        Owner.get().ModulesStore.emplace(module);
        Owner.get().CommitUnlocked(*module);
        ApplyPendingDMData(*module);
        ++materialized;
    }
    YDIAG(V) << "Materialized makefile group " << group.MakefileId << ": " << materialized
             << " of " << group.MemberCount << " frozen modules" << Endl;
}


void TFrozenModuleStore::Save(IOutputStream* output) {
    TSaveImageBuilder{*this}.Write(output);
}

void TFrozenModuleStore::SaveDMCache(IOutputStream* output, const TDepGraph& graph) {
    const auto moduleIds = LogicalModuleIds();
    ::Save(output, static_cast<ui32>(moduleIds.size()));
    for (const auto modId : moduleIds) {
        ::Save(output, modId);

        const auto moduleLists = Owner.get().GetModuleNodeLists(modId);
        TDMCacheRecord record;
        for (auto peer : moduleLists.UniqPeers()) {
            record.UniqPeersIds.push_back(graph.Get(peer)->ElemId);
        }
        for (auto peer : moduleLists.ManagedDirectPeers()) {
            record.ManagedDirectPeersIds.push_back(graph.Get(peer)->ElemId);
        }

        TDMData dmData;
        if (const auto module = Owner.get().ModulesById.FindPtr(modId)) {
            dmData.Vars.reserve(DM_VAR_COUNT);
            for (const auto varName : DM_VAR_NAMES) {
                dmData.Vars.emplace_back((*module)->Get(varName));
            }
            dmData.PeersComplete = (*module)->IsPeersComplete();
        } else if (const auto* pending = FindPendingDMData(modId)) {
            dmData = ExpandPendingDMData(*pending);
        } else {
            const ui64 descriptor = FindDescriptor(modId);
            Y_ENSURE(descriptor != NoIndex, "Missing frozen module while saving dependency management cache");
            // Embedded defaults contain inherited values from the writer's
            // configuration. Resolve inheritance now, exactly as a late Get
            // would, without constructing or committing a TModule.
            const auto state = LoadModuleState(Descriptor(descriptor));
            dmData = ReloadDMData(state.ConfigVars, state.MakefileId);
        }

        for (const auto [varIndex, varName] : Enumerate(DM_VAR_NAMES)) {
            if (!dmData.Vars[varIndex].empty()) {
                record.Vars.emplace(varName, dmData.Vars[varIndex]);
            }
        }
        record.PeersComplete = dmData.PeersComplete;
        ::Save(output, record);
    }
}

TVector<TFileElemId> TFrozenModuleStore::LogicalModuleIds() const {
    TVector<TFileElemId> result;
    result.reserve(Header.ModuleCount + Owner.get().ModulesById.size());
    for (ui64 i = 0; i < Header.ModuleCount; ++i) {
        const auto descriptor = Descriptor(i);
        const TFileElemId id(descriptor.ModuleId);
        if (!Owner.get().ModulesById.contains(id) && !SuppressedModules.contains(id) && !Owner.get().ReparsedMakefiles.contains(TFileElemId(descriptor.MakefileId))) {
            result.push_back(id);
        }
    }
    for (const auto [id, module] : Owner.get().ModulesById) {
        if (!(Owner.get().Loaded && module->IsLoaded() && Owner.get().ReparsedMakefiles.contains(module->GetMakefileId()))) {
            result.push_back(id);
        }
    }
    SortUnique(result);
    return result;
}

void TFrozenModuleStore::AccumulateStats(ui64& total, ui64& loaded, ui64& outdated) const {
    for (ui64 i = 0; i < Header.ModuleCount; ++i) {
        const auto descriptor = Descriptor(i);
        const TFileElemId id(descriptor.ModuleId);
        if (Owner.get().ModulesById.contains(id) || SuppressedModules.contains(id)) {
            continue;
        }
        ++loaded;
        if (Owner.get().ReparsedMakefiles.contains(TFileElemId(descriptor.MakefileId))) {
            ++outdated;
        } else {
            ++total;
        }
    }
}

NFrozenModuleStore::TModuleDescriptor TFrozenModuleStore::Descriptor(ui64 index) const {
    Y_ENSURE(index < Header.ModuleCount, "Frozen module descriptor is out of bounds");
    NFrozenModuleStore::TModuleDescriptor descriptor;
    const size_t size = SerializedSize(descriptor);
    TMemoryInput input(Image.AsCharPtr() + DescriptorsOffset + index * size, size);
    ::Load(&input, descriptor);
    Y_ENSURE(input.Exhausted(), "Invalid frozen module descriptor size");
    return descriptor;
}

NFrozenModuleStore::TGroupDescriptor TFrozenModuleStore::Group(ui64 index) const {
    Y_ENSURE(index < Header.GroupCount, "Frozen module group is out of bounds");
    NFrozenModuleStore::TGroupDescriptor group;
    const size_t size = SerializedSize(group);
    TMemoryInput input(Image.AsCharPtr() + GroupsOffset + index * size, size);
    ::Load(&input, group);
    Y_ENSURE(input.Exhausted(), "Invalid frozen module group size");
    return group;
}

ui32 TFrozenModuleStore::GroupMember(ui64 index) const {
    Y_ENSURE(index < Header.ModuleCount, "Frozen module group member is out of bounds");
    ui32 member = 0;
    const size_t size = SerializedSize(member);
    TMemoryInput input(Image.AsCharPtr() + MembersOffset + index * size, size);
    ::Load(&input, member);
    Y_ENSURE(input.Exhausted(), "Invalid frozen module group member size");
    return member;
}

ui64 TFrozenModuleStore::FindDescriptor(TFileElemId id) const {
    const ui32 rawId = RawElemId(id);
    const auto indexes = xrange(Header.ModuleCount);
    const auto it = LowerBoundBy(indexes.begin(), indexes.end(), rawId, [this](ui64 index) {
        return Descriptor(index).ModuleId;
    });
    return it != indexes.end() && Descriptor(*it).ModuleId == rawId ? *it : NoIndex;
}

ui64 TFrozenModuleStore::FindGroup(TFileElemId makefileId) const {
    const ui32 rawId = RawElemId(makefileId);
    const auto indexes = xrange(Header.GroupCount);
    const auto it = LowerBoundBy(indexes.begin(), indexes.end(), rawId, [this](ui64 index) {
        return Group(index).MakefileId;
    });
    return it != indexes.end() && Group(*it).MakefileId == rawId ? *it : NoIndex;
}

const TFrozenModuleStore::TPendingDMData* TFrozenModuleStore::FindPendingDMData(TFileElemId id) const {
    const auto it = LowerBoundBy(PendingDMData.begin(), PendingDMData.end(), id, [](const TPendingDMData& item) {
        return item.ModuleId;
    });
    return it != PendingDMData.end() && it->ModuleId == id ? &*it : nullptr;
}

void TFrozenModuleStore::ApplyPendingDMData(TModule& module) {
    const auto* pending = FindPendingDMData(module.GetId());
    if (!pending) {
        return;
    }
    for (const auto [varIndex, varName] : Enumerate(DM_VAR_NAMES)) {
        module.Set(varName, PendingDMValue(*pending, varIndex));
    }
    if (pending->PeersComplete && !module.IsPeersComplete()) {
        module.SetPeersComplete();
    }
}

TStringBuf TFrozenModuleStore::PendingDMValue(const TPendingDMData& data, size_t index) const {
    Y_ASSERT(index < DM_VAR_COUNT);
    const ui32 offset = data.VarOffsets[index];
    if (offset == 0) {
        return {};
    }
    Y_ENSURE(offset < PendingDMStrings.size(), "Invalid dependency management string offset");
    TMemoryInput input(PendingDMStrings.data() + offset, PendingDMStrings.size() - offset);
    ui32 size;
    ::Load(&input, size);
    Y_ENSURE(size <= input.Avail(), "Invalid dependency management string size");
    return TStringBuf(PendingDMStrings.data() + PendingDMStrings.size() - input.Avail(), size);
}

TFrozenModuleStore::TDMData TFrozenModuleStore::ExpandPendingDMData(const TPendingDMData& data) const {
    TDMData result;
    result.Vars.reserve(DM_VAR_COUNT);
    for (size_t i = 0; i < DM_VAR_COUNT; ++i) {
        result.Vars.emplace_back(PendingDMValue(data, i));
    }
    result.PeersComplete = data.PeersComplete;
    return result;
}

TFrozenModuleStore::TDMData TFrozenModuleStore::ReloadDMData(const TVector<TCmdElemId>& configVars, TFileElemId makefileId) const {
    // Reconstruct only the DM variables that TModule(saved) would expose.
    // ConfigVars is the committed configure snapshot; module.Vars may already
    // contain values from a later dependency-management calculation.
    TVars vars;
    vars.Id = makefileId;
    for (const auto varId : configVars) {
        const auto property = Owner.get().Symbols.CmdNameById(varId).GetStr();
        const auto name = GetPropertyName(property);
        for (const auto dmName : DM_VAR_NAMES) {
            if (name == dmName) {
                vars.SetValue(name, GetPropertyValue(property));
                break;
            }
        }
    }
    TDMData result;
    result.Vars.reserve(DM_VAR_COUNT);
    for (const auto name : DM_VAR_NAMES) {
        const TYVar* value = nullptr;
        if (const auto it = vars.find(name); it != vars.end()) {
            value = &it->second;
        } else {
            // Saving may overlap rendering. TVars::Lookup writes TYVar::Id
            // even for const access; inspect inherited values without that
            // side effect on the shared command configuration.
            for (const TVars* scope = &Owner.get().CreationContext.CommandConf; scope; scope = scope->Base) {
                if (const auto it = scope->find(name); it != scope->end()) {
                    value = &it->second;
                    break;
                }
            }
        }
        result.Vars.emplace_back(GetCmdValue(::Get1(value)));
    }
    return result;
}

TFrozenModuleStore::TDMData TFrozenModuleStore::LoadFrozenDMData(ui64 descriptorIndex) const {
    const auto descriptor = Descriptor(descriptorIndex);
    TMemoryInput input(PayloadData(descriptor.DMOffset, descriptor.DMSize), descriptor.DMSize);
    TDMData result;
    ::Load(&input, result);
    Y_ENSURE(result.Vars.size() == DM_VAR_COUNT, "Invalid frozen module dependency management data");
    Y_ENSURE(input.Exhausted(), "Trailing data in frozen module dependency management record");
    return result;
}

const char* TFrozenModuleStore::PayloadData(ui64 offset, ui64 size) const {
    const ui64 payloadSize = Image.Size() - PayloadOffset;
    Y_ENSURE(offset <= payloadSize && size <= payloadSize - offset, "Frozen module payload range is out of bounds");
    return Image.AsCharPtr() + PayloadOffset + offset;
}
