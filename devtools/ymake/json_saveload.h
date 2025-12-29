#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>

#include "prop_names.h"
#include "md5.h"

class TSaveBuffer {
private:
    TVector<ui8>* Buffer;

public:
    TSaveBuffer(TVector<ui8>* buffer) : Buffer(buffer) {
        buffer->clear();
    }

    template<typename T>
    void Save(const T& value) noexcept {
        Save(&value, sizeof(T));
    }

    void Save(const void* ptr, size_t size) noexcept {
        size_t oldSize = Buffer->size();
        size_t newSize = oldSize + size;
        Buffer->yresize(newSize);
        memcpy(Buffer->data() + oldSize, ptr, size);
    }

    void SaveElemId(TNodeId nodeId, const TDepGraph& graph) noexcept {
        if (nodeId == TNodeId::Invalid) {
            Save<ui8>(0);
            Save<ui32>(0);
            return;
        }
        auto node = graph.Get(nodeId);
        ui8 useFileId = UseFileId(node->NodeType) ? 1 : 0;
        Save<ui8>(useFileId);
        Save<ui32>(node->ElemId);
    }

    void SaveElemIds(const THolder<TUniqVector<TNodeId>>& nodeIdsHolder, const TDepGraph& graph) noexcept {
        if (!nodeIdsHolder.Get()) {
            Save<ui32>(0);
            return;
        }

        const TUniqVector<TNodeId>* nodeIds = nodeIdsHolder.Get();
        Save<ui32>(nodeIds->size());
        for (TNodeId nodeId : *nodeIds) {
            SaveElemId(nodeId, graph);
        }
    }

private:
    struct TURVScratchpad {
        TURVScratchpad() {
            varNodeName.reserve(1024);
            varNodeName.append(NProps::USED_RESERVED_VAR);
            varNodeName.append('=');
            namePrefixSize = varNodeName.size();
        }
        TString varNodeName;
        size_t namePrefixSize;
    };

    ui32 FindUsedReservedVar(TURVScratchpad& scratchpad, TStringBuf varName, const TDepGraph& graph) {
        // "USED_RESERVED_VAR=" + varName
        scratchpad.varNodeName.replace(scratchpad.namePrefixSize, scratchpad.varNodeName.size() - scratchpad.namePrefixSize, varName);

        ui32 varElemId = graph.Names().CommandConf.GetIdNx(scratchpad.varNodeName);
        Y_ASSERT(varElemId != 0); // guaranteed by TModuleBuilder::AddGlobalVarDep
        return varElemId;
    }

public:
    void SaveReservedVars(const THashSet<TString>* usedReservedVars, const TDepGraph& graph) {
        if (!usedReservedVars) {
            Save<ui32>(0);
            return;
        }

        Save<ui32>(usedReservedVars->size());
        TURVScratchpad scratchpad;
        for (const auto& varName : *usedReservedVars) {
            Save<ui32>(FindUsedReservedVar(scratchpad, varName, graph));
        }
    }

    void SaveReservedVarsTotals(const THashMap<TString, THashSet<TString>>* usedReservedVarsTotals, const TDepGraph& graph) {
        if (!usedReservedVarsTotals) {
            Save<ui32>(0);
            return;
        }

        Save<ui32>(usedReservedVarsTotals->size());
        TURVScratchpad scratchpad;
        for (const auto& usedReservedVars : *usedReservedVarsTotals) {
            Save<ui32>(FindUsedReservedVar(scratchpad, usedReservedVars.first, graph));
            Save<ui32>(usedReservedVars.second.size());
            for (const auto& varName : usedReservedVars.second) {
                Save<ui32>(FindUsedReservedVar(scratchpad, varName, graph));
            }
        }
    }

    void SaveToStream(IOutputStream* out) {
        ui32 size = Buffer->size();
        out->Write(&size, sizeof(size));
        out->Write(Buffer->data(), size);
    }

    void SaveNodeDataToStream(IOutputStream* output, TNodeId nodeId, const TDepGraph& graph) {
        auto nodeRef = graph.Get(nodeId);

        ui8 useFileId = UseFileId(nodeRef->NodeType) ? 1 : 0;
        ui32 elemId = nodeRef->ElemId;
        output->Write(&useFileId, sizeof(useFileId));
        output->Write(&elemId, sizeof(elemId));
        SaveToStream(output);
    }
};

class TFileFormatError : public yexception {
public:
    TFileFormatError() {
        Append("Wrong file format"sv);
    }
};

template<typename T>
inline T LoadFromStream(IInputStream* input) {
    T value;
    size_t bytesRead = input->Load(&value, sizeof(value));
    if (bytesRead != sizeof(value))
        ythrow TFileFormatError{};
    return value;
}

inline void LoadBufferFromStream(IInputStream* input, void* buffer, size_t size) {
    size_t bytesRead = input->Load(buffer, size);
    if (bytesRead != size)
        ythrow TFileFormatError{};
}

class TLoadBuffer {
private:
    TVector<ui8>* RawBuffer;
    const ui8* Iterator = nullptr;
    const ui8* End = nullptr;

    void LoadRawBufferFromStream(IInputStream* input, size_t size) {
        RawBuffer->yresize(size);
        LoadBufferFromStream(input, RawBuffer->data(), size);
        Iterator = RawBuffer->data();
        End = Iterator + size;
    }

public:
    enum ELoadResult {
        InvalidResult,
        NodeLoaded,
        NodeChangedAndHeaderLoaded,
        NodeNotValid,
    };

    TLoadBuffer(TVector<ui8>* rawBuffer) : RawBuffer(rawBuffer) {}

    ELoadResult LoadUnchangedNodeDataFromStream(IInputStream* input, TNodeId& nodeId, const TDepGraph& graph, size_t onSkipHeaderSize = 0) {
        ui8 useFileId = LoadFromStream<ui8>(input);
        ui32 elemId = LoadFromStream<ui32>(input);
        ui32 size = LoadFromStream<ui32>(input);

        auto nodeRef = graph.GetNodeById(useFileId ? EMNT_File : EMNT_BuildCommand, elemId);
        nodeId = nodeRef.Id();

        bool skipNode{};
        bool loadHeaderWhenSkipping{};
        ELoadResult loadResult = InvalidResult;

        if (nodeRef.IsValid()) {
            // TODO: Make separate usage of content and structure changes flag.
            if (nodeRef.Value().State.HasNoChanges()) {
                skipNode = false;
                loadResult = NodeLoaded;
            } else {
                skipNode = true;
                loadHeaderWhenSkipping = true;
                loadResult = NodeChangedAndHeaderLoaded;
            }
        } else {
            skipNode = true;
            loadHeaderWhenSkipping = false;
            loadResult = NodeNotValid;
        }

        if (onSkipHeaderSize == 0) {
            loadHeaderWhenSkipping = false;
        }

        if (skipNode) {
            if (loadHeaderWhenSkipping) {
                Y_ASSERT(size >= onSkipHeaderSize);
                LoadRawBufferFromStream(input, onSkipHeaderSize);
                input->Skip(size - onSkipHeaderSize);
            } else {
                input->Skip(size);
            }
        } else {
            LoadRawBufferFromStream(input, size);
        }

        Y_ASSERT(loadResult != InvalidResult);
        return loadResult;
    }

    template<typename T>
    T Load() {
        T value;
        if (!LoadBuffer(&value, sizeof(value)))
            ythrow TFileFormatError{};
        return value;
    }

    bool LoadBuffer(void* ptr, size_t size) noexcept {
        const ui8* nextIterator = Iterator + size;
        if (nextIterator > End)
            return false;

        memcpy(ptr, Iterator, size);
        Iterator = nextIterator;
        return true;
    }

    inline bool LoadElemId(TNodeId* nodeId, const TDepGraph& graph) {
        ui8 useFileId = Load<ui8>();
        ui32 elemId = Load<ui32>();

        if (useFileId == 0 && elemId == 0) {
            *nodeId = TNodeId::Invalid;
            return true;
        }

        auto node = graph.GetNodeById(useFileId ? EMNT_File : EMNT_BuildCommand, elemId);
        if (!node.IsValid())
            return false;

        *nodeId = node.Id();
        return true;
    }

    bool LoadElemIds(THolder<TUniqVector<TNodeId>>* nodeIdsHolder, const TDepGraph& graph) {
        ui32 count = Load<ui32>();

        if (count == 0) {
            nodeIdsHolder->Reset();
            return true;
        } else {
            nodeIdsHolder->Reset(new TUniqVector<TNodeId>{});
            TUniqVector<TNodeId>* rawNodeIds = nodeIdsHolder->Get();
            for (size_t i = 0; i < count; ++i) {
                TNodeId nodeId;
                if (!LoadElemId(&nodeId, graph))
                    return false;
                rawNodeIds->Push(nodeId);
            }
            return true;
        }
    }

private:
    struct TURVHelpers {
        TURVHelpers() {
            prefix.reserve(64);
            prefix.append(NProps::USED_RESERVED_VAR);
            prefix.append('=');
            prefixSize = prefix.size();
        }
        TString prefix;
        size_t prefixSize;
    };

    TMaybe<TString> FindUsedReservedVar(const TURVHelpers& helpers, ui32 elemId, const TDepGraph& graph) {
        auto cmdView = graph.Names().CommandConf.GetName(elemId);
        if (!cmdView.IsValid())
            return {};

        TStringBuf cmd = cmdView.GetStr();
        if (!cmd.StartsWith(helpers.prefix))
            return {};

        TStringBuf varName = cmd.SubString(helpers.prefixSize, cmd.size() - helpers.prefixSize);
        return TString{varName};
    }

public:
    bool LoadReservedVars(THolder<THashSet<TString>>* varsHolder, const TDepGraph& graph) {
        ui32 count = Load<ui32>();

        if (count == 0) {
            varsHolder->Reset();
            return true;
        }

        varsHolder->Reset(new THashSet<TString>());
        auto* vars = varsHolder->Get();

        TURVHelpers helpers;

        for (size_t i = 0; i < count; ++i) {
            ui32 elemId = Load<ui32>();
            auto var = FindUsedReservedVar(helpers, elemId, graph);
            if (!var)
                return false;
            vars->insert(*var);
        }

        return true;
    }

    bool LoadReservedVarsTotals(THolder<THashMap<TString, THashSet<TString>>>* varsHolder, const TDepGraph& graph) {
        ui32 count = Load<ui32>();

        if (count == 0) {
            varsHolder->Reset();
            return true;
        }

        varsHolder->Reset(new THashMap<TString, THashSet<TString>>());
        auto* vars = varsHolder->Get();

        TURVHelpers helpers;

        for (size_t i = 0; i < count; ++i) {
            ui32 elemId = Load<ui32>();
            auto var = FindUsedReservedVar(helpers, elemId, graph);
            if (!var)
                return false;
            auto varInserted = vars->insert({*var, {}});
            ui32 subCount = Load<ui32>();
            for (size_t j = 0; j < subCount; ++j) {
                ui32 subElemId = Load<ui32>();
                auto subVar = FindUsedReservedVar(helpers, subElemId, graph);
                if (!subVar)
                    return false;
                varInserted.first->second.insert(*subVar);
            }
        }

        return true;
    }

    void LoadMd5(TMd5SigValue* result) {
        ui8 rawData[16];
        if (!LoadBuffer(rawData, 16))
            ythrow TFileFormatError{};

        result->SetRawData(rawData, "Load from cache"sv);
    }
};
