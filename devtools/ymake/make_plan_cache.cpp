#include "make_plan_cache.h"

#include "json_visitor.h"
#include "saveload.h"

#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/diag/stats.h>

#include <devtools/ymake/make_plan/make_plan.h>

#include <library/cpp/on_disk/multi_blob/multiblob_builder.h>

#include <mutex>
#include <util/ysaveload.h>
#include <util/generic/algorithm.h>
#include <util/generic/fwd.h>
#include <util/generic/hash.h>
#include <util/generic/maybe.h>
#include <util/generic/overloaded.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>
#include <util/memory/blob.h>
#include <util/random/random.h>
#include <util/stream/file.h>
#include <util/string/join.h>
#include <util/string/split.h>
#include <variant>

namespace {
    const TStringBuf JOINED_START("[");
    const TStringBuf JOINED_END("]");
    const TStringBuf JOINED_PART_START("\"");
    const TStringBuf JOINED_PART_END("\"");
    const TStringBuf JOINED_PART_SEPARATOR("\", \"");
}

namespace NCache {
        template <typename TStrType>
        void TConversionContext::Convert(const TCached& src, TStrType& dst) const {
            TStringBuf buffer = Names_.GetName<TCmdView>(src).GetStr();
            dst = buffer;
        }

        TStringBuf TConversionContext::GetBuf(const TCached& src) const {
            return Names_.GetName<TCmdView>(src).GetStr();
        }

        template <typename TStrType>
        void TConversionContext::Convert(TStrType&& src, TCached& dst) {
            dst = IdGetter_(std::forward<TStrType>(src), Names_);
        }

        void TConversionContext::Convert(const TOriginal& src, TJoinedCached& dst) {
            TStringBuf srcFixed(src);
            srcFixed.SkipPrefix(JOINED_START);
            srcFixed.ChopSuffix(JOINED_END);
            srcFixed.SkipPrefix(JOINED_PART_START);
            srcFixed.ChopSuffix(JOINED_PART_END);

            if (srcFixed.empty()) {
                return;
            }

            TVector<TStringBuf> parts;
            StringSplitter(srcFixed).SplitByString(JOINED_PART_SEPARATOR).Collect(&parts);

            dst.resize(parts.size());
            for (size_t i = 0; i < parts.size(); i++) {
                Convert(parts[i], dst[i]);
            }
        }

        void TConversionContext::Convert(const TVector<TOriginal>& src, TJoinedCached& dst) {
            dst.resize(src.size());
            for (size_t i = 0; i < src.size(); i++) {
                Convert(src[i], dst[i]);
            }
        }

        void TConversionContext::Convert(const TJoinedCommand& src, TJoinedCachedCommand& dst) {
            std::visit(TOverloaded{
                [&](const TOriginal& src)          { dst.Tag = 0; Convert(src, dst.Data); },
                [&](const TVector<TOriginal>& src) { dst.Tag = 1; Convert(src, dst.Data); }
            }, src);
        }

        void TConversionContext::Convert(const TJoinedCached& src, TOriginal& dst) {
            TVector<TStringBuf> parts;
            parts.reserve(src.size());
            for (const auto& srcElem : src) {
                parts.emplace_back();
                Convert(srcElem, parts.back());
            }

            TStringBuilder builder;
            builder << JOINED_START;
            if (!parts.empty()) {
                builder << JOINED_PART_START;
            }
            builder << MakeRangeJoiner(JOINED_PART_SEPARATOR, parts);
             if (!parts.empty()) {
                 builder << JOINED_PART_END;
             }
            builder << JOINED_END;
            dst = builder;
        }

        void TConversionContext::Convert(const TJoinedCached& src, TVector<TOriginal>& dst) {
            dst.reserve(src.size());
            for (const auto& srcElem : src) {
                dst.emplace_back();
                Convert(srcElem, dst.back());
            }
        }

        void TConversionContext::Convert(const TJoinedCachedCommand& src, TJoinedCommand& dst) {
            switch(src.Tag) {
                case 0: {
                    dst = TOriginal();
                    Convert(src.Data, std::get<TOriginal>(dst));
                    break;
                }
                case 1: {
                    dst = TVector<TOriginal>();
                    Convert(src.Data, std::get<TVector<TOriginal>>(dst));
                    break;
                }
                default:
                    throw yexception();
            }
        }

        template <typename TSrc, typename TDst>
        void TConversionContext::Convert(const TVector<TSrc>& src, TVector<TDst>& dst) {
            dst.clear();
            dst.reserve(src.size());

            for (const auto& srcElem : src) {
                TDst buffer;
                Convert(srcElem, buffer);
                dst.push_back(std::move(buffer));
            }
        }

        template <typename TSrc, typename TDst>
        void TConversionContext::Convert(const TMaybe<TSrc>& src, TMaybe<TDst>& dst) {
            if (src.Empty()) {
                dst = {};
            } else {
                TDst buffer;
                Convert(src.GetRef(), buffer);
                dst = buffer;
            }
        }

        template <typename TSrc, typename TDst>
        void TConversionContext::Convert(const TKeyValueMap<TSrc>& src, TKeyValueMap<TDst>& dst) {
            dst.clear();
            dst.reserve(src.size());
            for (const auto& [key, value] : src) {
                TDst keyOriginal, valueOriginal;
                Convert(key, keyOriginal);
                Convert(value, valueOriginal);
                dst.emplace(std::move(keyOriginal), std::move(valueOriginal));
            }
        }

        template <typename TSrc, typename TJoinedSrc, typename TDst, typename TJoinedDst>
        void TConversionContext::Convert(const TMakeCmdDescription<TSrc, TJoinedSrc>& src, TMakeCmdDescription<TDst, TJoinedDst>& dst) {
            Convert(src.CmdArgs, dst.CmdArgs);
            Convert(src.Env, dst.Env);
            Convert(src.Cwd, dst.Cwd);
            Convert(src.StdOut, dst.StdOut);
        }

        template <
            typename TSrc, typename TJoinedSrc, typename TJoinedCmdSrc,
            typename TDst, typename TJoinedDst, typename TJoinedCmdDst
        >
        void TConversionContext::Convert(
            const TMakeNodeDescription<TSrc, TJoinedSrc, TJoinedCmdSrc>& src,
            TMakeNodeDescription<TDst, TJoinedDst, TJoinedCmdDst>& dst
        ) {
            Convert(src.SelfUid, dst.SelfUid);
            Convert(src.Cmds, dst.Cmds);
            Convert(src.Deps, dst.Deps);
            Convert(src.ToolDeps, dst.ToolDeps);
            Convert(src.KV, dst.KV);
            Convert(src.Requirements, dst.Requirements);
            Convert(src.TaredOuts, dst.TaredOuts);
            Convert(src.TargetProps, dst.TargetProps);
            Convert(src.OldEnv, dst.OldEnv);
            if (StoreInputs_) {
                Convert(src.Inputs, dst.Inputs);
            }
            Convert(src.Outputs, dst.Outputs);
            Convert(src.LateOuts, dst.LateOuts);
            Convert(src.ResourceUris, dst.ResourceUris);
        }
}

namespace {
    inline void WriteCachedJoinedArrToMap(TJsonWriterFuncArgs&& funcArgs, const NCache::TJoinedCached& cachedArr) {
        auto& writer = funcArgs.Writer;
        const auto* context = funcArgs.Context;
        writer.WriteMapKey(funcArgs.Map, funcArgs.Key);
        writer.WriteJsonValue(JOINED_START);
        if (!cachedArr.empty()) {
            writer.WriteJsonValue(JOINED_PART_START);
            for (const auto& cachedItem : cachedArr) {
                writer.WriteJsonValue(context->GetBuf(cachedItem));
                if (&cachedItem != &cachedArr.back()) {
                    writer.WriteJsonValue(JOINED_PART_SEPARATOR);
                }
            }
            writer.WriteJsonValue(JOINED_PART_END);
        }
        writer.WriteJsonValue(JOINED_END);
    }

    inline void WriteCachedArrToMap(TJsonWriterFuncArgs&& funcArgs, const NCache::TJoinedCached& cachedArr) {
        auto& writer = funcArgs.Writer;
        const auto* context = funcArgs.Context;
        writer.WriteMapKey(funcArgs.Map, funcArgs.Key);
        auto arr = writer.OpenArray();
        for (const auto cachedItem : cachedArr) {
            writer.WriteArrayValue(arr, context->GetBuf(cachedItem));
        }
        writer.CloseArray(arr);
    }

    inline void WriteCachedMapToMap(TJsonWriterFuncArgs&& funcArgs, const TKeyValueMap<NCache::TCached>& cachedMap) {
        auto& writer = funcArgs.Writer;
        const auto* context = funcArgs.Context;
        writer.WriteMapKey(funcArgs.Map, funcArgs.Key);
        auto subMap = writer.OpenMap();
        for (const auto [cachedKey, cachedValue] : cachedMap) {
            writer.WriteMapKeyValue(subMap, context->GetBuf(cachedKey), context->GetBuf(cachedValue));
        }
        writer.CloseMap(subMap);
    }
}

TMd5Sig JsonConfHash(const TBuildConfiguration& conf) {
    auto fakeIdValue = conf.CommandConf.Get1("JSON_CACHE_FAKE_ID");
    if (fakeIdValue.empty()) {
        return conf.YmakeConfWoRulesMD5;
    }
    TMd5Sig fakeIdMd5Sig;
    MD5 md5;
    md5.Update(fakeIdValue);
    md5.Final(fakeIdMd5Sig.RawData);
    return fakeIdMd5Sig;
}

TMakeNodeSavedState::TMakeNodeSavedState(const TMakeNode& node, const TStringBuf& nodeName, const TStringBuf& nodeCacheUid, const TStringBuf& nodeRenderId, const TBuildConfiguration& conf, NCache::TConversionContext& context) {
    context.Convert(nodeCacheUid, CachedNode.Uid);
    context.Convert(node, CachedNode);
    context.Convert(nodeName, InvalidationId);
    context.Convert(nodeRenderId, PartialMatchId);
    StrictInputs = conf.DumpInputsInJSON;
}

void TMakeNodeSavedState::Restore(NCache::TConversionContext& context, TMakeNode* result) const {
    context.Convert(CachedNode.Uid, result->Uid);
    context.Convert(CachedNode, *result);
}

TVector<NCache::TOriginal> TMakeNodeSavedState::RestoreLateOuts(NCache::TConversionContext& context) const {
    TVector<NCache::TOriginal> lateOuts;
    context.Convert(CachedNode.LateOuts, lateOuts);
    return lateOuts;
}

void TMakeNodeSavedState::WriteAsJson(NYMake::TJsonWriter& writer, const NCache::TConversionContext* context) const {
    CachedNode.WriteAsJson(writer, context);
}

template <>
bool TMakeCmdCached::Empty() const {
    return CmdArgs.Data.empty();
}

template <>
void TMakeCmdCached::WriteCmdArgsArr(TJsonWriterFuncArgs&& funcArgs) const {
    switch (CmdArgs.Tag) {
        case 0:
            WriteCachedJoinedArrToMap(std::forward<TJsonWriterFuncArgs>(funcArgs), CmdArgs.Data);
            return;
        case 1:
            WriteCachedArrToMap(std::forward<TJsonWriterFuncArgs>(funcArgs), CmdArgs.Data);
            return;
        default:
            throw yexception() << "Invalid CmdArgs.Tag = " << ToString<>(CmdArgs.Tag);
    }
}

template <>
void TMakeCmdCached::WriteCwdStr(TJsonWriterFuncArgs&& funcArgs) const {
    if (Cwd) {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, funcArgs.Context->GetBuf(*Cwd));
    }
}

template <>
void TMakeCmdCached::WriteStdoutStr(TJsonWriterFuncArgs&& funcArgs) const {
    if (StdOut) {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, funcArgs.Context->GetBuf(*StdOut));
    }
}

template <>
void TMakeCmdCached::WriteEnvMap(TJsonWriterFuncArgs&& funcArgs) const {
    if (!Env.empty()) {
        WriteCachedMapToMap(std::forward<TJsonWriterFuncArgs>(funcArgs), Env);
    }
}

template<>
void TMakeNodeCached::WriteUidStr(TJsonWriterFuncArgs&& funcArgs) const {
    const auto* context = funcArgs.Context;
    if (const auto* refreshedMakeNode = context->GetRefreshedMakeNode(); refreshedMakeNode) {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, refreshedMakeNode->Uid);
    } else {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, context->GetBuf(Uid));
    }
}

template<>
void TMakeNodeCached::WriteSelfUidStr(TJsonWriterFuncArgs&& funcArgs) const {
    const auto* context = funcArgs.Context;
    if (const auto* refreshedMakeNode = context->GetRefreshedMakeNode(); refreshedMakeNode) {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, refreshedMakeNode->SelfUid);
    } else {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, context->GetBuf(SelfUid));
    }
}

template<>
void TMakeNodeCached::WriteCmdsArr(TJsonWriterFuncArgs&& funcArgs) const {
    auto& writer = funcArgs.Writer;
    const auto* context = funcArgs.Context;
    writer.WriteMapKey(funcArgs.Map, funcArgs.Key);
    auto cmdArr = writer.OpenArray();
    for (const auto& cmd: Cmds) {
        if (cmd.Empty()) {
            continue;
        }
        writer.WriteArrayValue(cmdArr, cmd, context);
    }
    writer.CloseArray(cmdArr);
}

template<>
void TMakeNodeCached::WriteInputsArr(TJsonWriterFuncArgs&& funcArgs) const {
    const auto* context = funcArgs.Context;
    if (const auto* refreshedMakeNode = context->GetRefreshedMakeNode(); refreshedMakeNode && !context->GetStoreInputs()) {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, refreshedMakeNode->Inputs);
    } else {
        WriteCachedArrToMap(std::forward<TJsonWriterFuncArgs>(funcArgs), Inputs);
    }
}

template<>
void TMakeNodeCached::WriteOutputsArr(TJsonWriterFuncArgs&& funcArgs) const {
    WriteCachedArrToMap(std::forward<TJsonWriterFuncArgs>(funcArgs), Outputs);
}

template<>
void TMakeNodeCached::WriteDepsArr(TJsonWriterFuncArgs&& funcArgs) const {
    if (const auto* refreshedMakeNode = funcArgs.Context->GetRefreshedMakeNode(); refreshedMakeNode) {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, refreshedMakeNode->Deps);
    } else {
        WriteCachedArrToMap(std::forward<TJsonWriterFuncArgs>(funcArgs), Deps);
    }
}

template<>
void TMakeNodeCached::WriteForeignDepsArr(TJsonWriterFuncArgs&& funcArgs) const {
    if (!ToolDeps.empty()) {
        auto& writer = funcArgs.Writer;
        const auto* context = funcArgs.Context;
        writer.WriteMapKey(funcArgs.Map, funcArgs.Key);
        auto depsMap = writer.OpenMap();
        if (const auto* refreshedMakeNode = context->GetRefreshedMakeNode(); refreshedMakeNode) {
            writer.WriteMapKeyValue(depsMap, "tool", refreshedMakeNode->ToolDeps);
        } else {
            WriteCachedArrToMap({writer, depsMap, "tool", context}, ToolDeps);
        }
        writer.CloseMap(depsMap);
    }
}

template<>
void TMakeNodeCached::WriteKVMap(TJsonWriterFuncArgs&& funcArgs) const {
    WriteCachedMapToMap(std::forward<TJsonWriterFuncArgs>(funcArgs), KV);
}

template<>
void TMakeNodeCached::WriteRequirementsMap(TJsonWriterFuncArgs&& funcArgs) const {
    auto& writer = funcArgs.Writer;
    const auto* context = funcArgs.Context;
    writer.WriteMapKey(funcArgs.Map, funcArgs.Key);
    auto reqMap = writer.OpenMap();
    size_t intValue;
    for (const auto [cachedKey, cachedValue] : Requirements) {
        const auto bufKey = context->GetBuf(cachedKey);
        const auto bufValue = context->GetBuf(cachedValue);
        if (TryFromString<size_t>(bufValue, intValue)) {
            writer.WriteMapKeyValue(reqMap, bufKey, intValue);
        } else {
            writer.WriteMapKeyValue(reqMap, bufKey, bufValue);
        }
    }
    writer.CloseMap(reqMap);
}

template<>
void TMakeNodeCached::WriteEnvMap(TJsonWriterFuncArgs&& funcArgs) const {
    if (!OldEnv.empty()) {
        WriteCachedMapToMap(std::forward<TJsonWriterFuncArgs>(funcArgs), OldEnv);
    }
}

template<>
void TMakeNodeCached::WriteTargetPropertiesMap(TJsonWriterFuncArgs&& funcArgs) const {
    WriteCachedMapToMap(std::forward<TJsonWriterFuncArgs>(funcArgs), TargetProps);
}

template<>
void TMakeNodeCached::WriteResourcesMap(TJsonWriterFuncArgs&& funcArgs) const {
    if (!ResourceUris.empty()) {
        auto& writer = funcArgs.Writer;
        const auto* context = funcArgs.Context;
        writer.WriteMapKey(funcArgs.Map, funcArgs.Key);
        auto resArr = writer.OpenArray();
        for (const auto& uri : ResourceUris) {
            auto uriMap = writer.OpenMap(resArr);
            writer.WriteMapKeyValue(uriMap, "uri", context->GetBuf(uri));
            writer.CloseMap(uriMap);
        }
        writer.CloseArray(resArr);
    }
}

template<>
void TMakeNodeCached::WriteTaredOutputsArr(TJsonWriterFuncArgs&& funcArgs) const {
    if (!TaredOuts.empty()) {
        WriteCachedArrToMap(std::forward<TJsonWriterFuncArgs>(funcArgs), TaredOuts);
    }
}

bool TMakeNodeSavedState::TCacheId::operator==(const TMakeNodeSavedState::TCacheId& rhs) const {
    return std::tie(Id, StrictInputs) == std::tie(rhs.Id, rhs.StrictInputs);
}

TMakePlanCache::TMakePlanCache(const TBuildConfiguration& conf)
        : Conf(conf)
        , LoadFromCache(Conf.ReadJsonCache)
        , SaveToCache(Conf.WriteJsonCache)
        , LockCache(Conf.ParallelRendering)
        , CachePath(Conf.YmakeJsonCache)
        , ConversionContext_(MakeHolder<NCache::TConversionContext>(Names, Conf.StoreInputsInJsonCache))
{}

TMakePlanCache::~TMakePlanCache()
{}

bool TMakePlanCache::LoadFromFile() {
    if (!LoadFromCache) {
        return false;
    }

    if (!CachePath.Exists()) {
        return false;
    }

    NYMake::TTraceStage stage{"Load JSON cache"};

    TCacheFileReader cacheReader(Conf, false, false, JsonConfHash);
    if (cacheReader.Read(CachePath) != TCacheFileReader::EReadResult::Success) {
        return false;
    }

    TBlob& names = cacheReader.GetNextBlob();
    TBlob& nodes = cacheReader.GetNextBlob();
    Load(names, nodes);

    YDebug() << "Json cache has been loaded..." << Endl;
    return true;
}

TFsPath TMakePlanCache::SaveToFile() {
    if (!SaveToCache) {
        return {};
    }

    NYMake::TTraceStage stage{"Save JSON cache"};

    TCacheFileWriter cacheWriter(Conf, CachePath, JsonConfHash);
    Save(cacheWriter.GetBuilder());

    YDebug() << "Json cache has been saved..." << Endl;

    return cacheWriter.Flush(true);
}

bool TMakePlanCache::RestoreByCacheUid(const TStringBuf& uid, TMakeNode* result) {
    return RestoreNode(uid, false, result);
}

const TMakeNodeSavedState* TMakePlanCache::GetCachedNodeByCacheUid(const TStringBuf& uid) {
    return GetCachedNode(uid, false);
}

NCache::TConversionContext& TMakePlanCache::GetConversionContext(const TMakeNode* refreshedMakeNode) {
    ConversionContext_->SetRefreshedMakeNode(refreshedMakeNode);
    return *ConversionContext_;
}

NCache::TConversionContext TMakePlanCache::GetConstConversionContext(const TMakeNode* refreshedMakeNode) {
    auto context = NCache::TConversionContext(Names, Conf.StoreInputsInJsonCache, [](const TStringBuf& name, TNameStore& names) -> ui32 { return names.GetId(name); });
    context.SetRefreshedMakeNode(refreshedMakeNode);
    return context;
}

bool TMakePlanCache::RestoreByRenderId(const TStringBuf& renderId, TMakeNode* result) {
    return RestoreNode(renderId, true, result);
}

const TMakeNodeSavedState* TMakePlanCache::GetCachedNode(const TStringBuf& id, bool partialMatch) {
    Stats.Inc(partialMatch ? NStats::EJsonCacheStats::PartialMatchRequests : NStats::EJsonCacheStats::FullMatchRequests);
    TMakeNodeSavedState::TCacheId cacheId;
    if (!ConversionContext_->GetNames().Has(id)) {
        return nullptr;
    }
    ConversionContext_->Convert(id, cacheId.Id);
    cacheId.StrictInputs = Conf.DumpInputsInJSON;
    const auto& matchMap = partialMatch ? PartialMatchMap : FullMatchMap;
    auto restoredIt = matchMap.find(cacheId);
    if (restoredIt == matchMap.end()) {
        return nullptr;
    }
    Stats.Inc(partialMatch ? NStats::EJsonCacheStats::PartialMatchSuccess : NStats::EJsonCacheStats::FullMatchSuccess);
    return &restoredIt->second.get();
}

bool TMakePlanCache::RestoreNode(const TStringBuf& id, bool partialMatch, TMakeNode* result) {
    const auto* cachedNode = GetCachedNode(id, partialMatch);
    if (!cachedNode) {
        return false;
    }
    cachedNode->Restore(*ConversionContext_, result);
    return true;
}

void TMakePlanCache::AddRenderedNode(const TMakeNode& newNode, TStringBuf name, TStringBuf cacheUid, TStringBuf renderId) {
    Stats.Inc(NStats::EJsonCacheStats::AddedItems);
    NCache::TConversionContext context(Names, Conf.StoreInputsInJsonCache);
    if (SaveToCache) {
        // there actually may be two separate locks for AddedNodes and context,
        // but keep one for simplicity
        auto lock = LockContextIfNeeded();
        AddedNodes.emplace_back(newNode, name, cacheUid, renderId, Conf, context);
    }
}

std::unique_lock<TAdaptiveLock> TMakePlanCache::LockContextIfNeeded() {
    if (LockCache) {
        return std::unique_lock(ContextLock);
    }
    return {};
}

void TMakePlanCache::LoadFromContext(const TString& context) {
    if (!LoadFromCache || context.empty()) {
        return;
    }

    auto blob = TBlob::FromString(context);
    TSubBlobs blobs(blob);
    Y_ENSURE(blobs.size() == 2);
    Load(blobs[0], blobs[1]);
}

TString TMakePlanCache::SaveToContext() {
    if (!SaveToCache) {
        return {};
    }

    TMultiBlobBuilder builder;
    Save(builder);
    TString context;
    {
        TStringOutput output(context);
        builder.Save(output, static_cast<ui32>(EMF_INTERLAY));
        output.Finish();
    }
    return context;
}

void TMakePlanCache::Load(TBlob& namesBlob, TBlob& nodesBlob) {
    FullMatchMap.clear();
    PartialMatchMap.clear();

    Stats.Set(NStats::EJsonCacheStats::AddedItems, 0);
    AddedNodes.clear();

    Names.Load(namesBlob);

    RestoredNodes.clear();
    TMemoryInput nodes(nodesBlob.Data(), nodesBlob.Length());
    RestoredNodes.resize(::LoadSize(&nodes));
    ::LoadRange(&nodes, RestoredNodes.begin(), RestoredNodes.end());
    Stats.Set(NStats::EJsonCacheStats::LoadedItems, RestoredNodes.size());

    for (TMakeNodeSavedState& node : RestoredNodes) {
        FullMatchMap.emplace(TMakeNodeSavedState::TCacheId{node.CachedNode.Uid, node.StrictInputs}, std::reference_wrapper<TMakeNodeSavedState>(node));
        PartialMatchMap.emplace(TMakeNodeSavedState::TCacheId{node.PartialMatchId, node.StrictInputs}, std::reference_wrapper<TMakeNodeSavedState>(node));
    }

    Stats.Set(NStats::EJsonCacheStats::FullMatchLoadedItems, FullMatchMap.size());
    Stats.Set(NStats::EJsonCacheStats::PartialMatchLoadedItems, PartialMatchMap.size());
}

void TMakePlanCache::Save(TMultiBlobBuilder& builder) {
    THashSet<TMakeNodeSavedState::TCacheId> updatedNodes;
    updatedNodes.reserve(AddedNodes.size());
    for (const auto& added : AddedNodes) {
        updatedNodes.insert({added.InvalidationId, added.StrictInputs});
    }

    EraseIf(RestoredNodes, [&updatedNodes](const TMakeNodeSavedState& state) {
        return updatedNodes.contains(TMakeNodeSavedState::TCacheId{state.InvalidationId, state.StrictInputs});
    });

    auto* namesBuilder = new TMultiBlobBuilder();
    Names.Save(*namesBuilder);
    builder.AddBlob(namesBuilder);

    TString nodes;
    {
        TStringOutput nodesOutput(nodes);
        size_t totalSize = RestoredNodes.size() + AddedNodes.size();
        ::SaveSize(&nodesOutput, totalSize);
        ::SaveRange(&nodesOutput, RestoredNodes.begin(), RestoredNodes.end());
        ::SaveRange(&nodesOutput, AddedNodes.begin(), AddedNodes.end());
        Stats.Set(NStats::EJsonCacheStats::OldItemsSaved, RestoredNodes.size());
        Stats.Set(NStats::EJsonCacheStats::NewItemsSaved, AddedNodes.size());
        Stats.Set(NStats::EJsonCacheStats::TotalItemsSaved, totalSize);
        nodesOutput.Finish();
    }

    builder.AddBlob(new TBlobSaverMemory(TBlob::Copy(nodes.data(), nodes.size())));
}

TString TMakePlanCache::GetStatistics() const {
    Stats.Report();

    TStringBuilder builder;
    builder << "JSON cache report: ";
    if (!SaveToCache && !LoadFromCache) {
        builder << "disabled";
        return builder;
    } else if (!SaveToCache) {
        builder << "read-only, ";
    } else if (!LoadFromCache) {
        builder << "write-only, ";
    } else {
        builder << "read-write, ";
    }

    size_t updated = Stats.Get(NStats::EJsonCacheStats::LoadedItems) - Stats.Get(NStats::EJsonCacheStats::OldItemsSaved);

    builder << Stats.Get(NStats::EJsonCacheStats::LoadedItems) << " nodes restored, ";
    builder << Stats.Get(NStats::EJsonCacheStats::FullMatchSuccess) << " full matches, ";
    builder << Stats.Get(NStats::EJsonCacheStats::PartialMatchSuccess) << " partial matches, ";
    builder << Stats.Get(NStats::EJsonCacheStats::FullyRendered) << " rendered from scratch, ";
    builder << updated << " updated";
    return builder;
}
