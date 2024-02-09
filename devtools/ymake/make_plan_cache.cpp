#include "make_plan_cache.h"

#include "json_visitor.h"
#include "saveload.h"

#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/diag/stats.h>

#include <devtools/ymake/make_plan/make_plan.h>

#include <library/cpp/on_disk/multi_blob/multiblob_builder.h>

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
    struct TConversionContext {
        TNameStore& Names;

        explicit TConversionContext(TNameStore& names)
                : Names(names)
        {
        }

        template <typename TStrType>
        void Convert(const TCached& src, TStrType& dst) {
            TStringBuf buffer = Names.GetName<TCmdView>(src).GetStr();
            dst = buffer;
        }

        template <typename TStrType>
        void Convert(TStrType&& src, TCached& dst) {
            dst = Names.Add(std::forward<TStrType>(src));
        }

        void Convert(const TOriginal& src, TJoinedCached& dst) {
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

        void Convert(const TVector<TOriginal>& src, TJoinedCached& dst) {
            dst.resize(src.size());
            for (size_t i = 0; i < src.size(); i++) {
                Convert(src[i], dst[i]);
            }
        }

        void Convert(const TJoinedCommand& src, TJoinedCachedCommand& dst) {
            std::visit(TOverloaded{
                [&](const TOriginal& src)          { dst.Tag = 0; Convert(src, dst.Data); },
                [&](const TVector<TOriginal>& src) { dst.Tag = 1; Convert(src, dst.Data); }
            }, src);
        }

        void Convert(const TJoinedCached& src, TOriginal& dst) {
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

        void Convert(const TJoinedCached& src, TVector<TOriginal>& dst) {
            dst.reserve(src.size());
            for (const auto& srcElem : src) {
                dst.emplace_back();
                Convert(srcElem, dst.back());
            }
        }

        void Convert(const TJoinedCachedCommand& src, TJoinedCommand& dst) {
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
        void Convert(const TVector<TSrc>& src, TVector<TDst>& dst) {
            dst.clear();
            dst.reserve(src.size());

            for (const auto& srcElem : src) {
                TDst buffer;
                Convert(srcElem, buffer);
                dst.push_back(std::move(buffer));
            }
        }

        template <typename TSrc, typename TDst>
        void Convert(const TMaybe<TSrc>& src, TMaybe<TDst>& dst) {
            if (src.Empty()) {
                dst = {};
            } else {
                TDst buffer;
                Convert(src.GetRef(), buffer);
                dst = buffer;
            }
        }

        template <typename TSrc, typename TDst>
        void Convert(const TKeyValueMap<TSrc>& src, TKeyValueMap<TDst>& dst) {
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
        void Convert(const TMakeCmdDescription<TSrc, TJoinedSrc>& src, TMakeCmdDescription<TDst, TJoinedDst>& dst) {
            Convert(src.CmdArgs, dst.CmdArgs);
            Convert(src.Env, dst.Env);
            Convert(src.Cwd, dst.Cwd);
            Convert(src.StdOut, dst.StdOut);
        }

        template <
            typename TSrc, typename TJoinedSrc, typename TJoinedCmdSrc,
            typename TDst, typename TJoinedDst, typename TJoinedCmdDst
        >
        void Convert(
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
            Convert(src.Inputs, dst.Inputs);
            Convert(src.Outputs, dst.Outputs);
            Convert(src.LateOuts, dst.LateOuts);
            Convert(src.ResourceUris, dst.ResourceUris);
        }
    };
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

bool TMakeNodeSavedState::TCacheId::operator==(const TMakeNodeSavedState::TCacheId& rhs) const {
    return std::tie(Id, StrictInputs) == std::tie(rhs.Id, rhs.StrictInputs);
}

TMakePlanCache::TMakePlanCache(const TBuildConfiguration& conf)
        : Conf(conf)
        , LoadFromCache(Conf.ReadJsonCache)
        , SaveToCache(Conf.WriteJsonCache)
        , CachePath(Conf.YmakeJsonCache)
{
}

void TMakePlanCache::LoadFromFile() {
    if (!LoadFromCache) {
        return;
    }
    auto loadFailed = []() {
        NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::UsedJSONCache), false); // enabled, but not loaded
    };

    if (!CachePath.Exists()) {
        return loadFailed();
    }

    TCacheFileReader cacheReader(Conf, false, JsonConfHash);
    if (cacheReader.Read(CachePath) != TCacheFileReader::EReadResult::Success) {
        return loadFailed();
    }

    TBlob& names = cacheReader.GetNextBlob();
    TBlob& nodes = cacheReader.GetNextBlob();
    Load(names, nodes);

    YDebug() << "Json cache has been loaded..." << Endl;
    NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::UsedJSONCache), true);
}

TFsPath TMakePlanCache::SaveToFile() {
    if (!SaveToCache) {
        return {};
    }

    TCacheFileWriter cacheWriter(Conf, CachePath, JsonConfHash);
    Save(cacheWriter.GetBuilder());

    YDebug() << "Json cache has been saved..." << Endl;

    return cacheWriter.Flush(true);
}

bool TMakePlanCache::RestoreByCacheUid(const TStringBuf& uid, TMakeNode* result) {
    return RestoreNode(uid, false, result);
}

bool TMakePlanCache::RestoreByRenderId(const TStringBuf& renderId, TMakeNode* result) {
    return RestoreNode(renderId, true, result);
}

bool TMakePlanCache::RestoreNode(const TStringBuf& id, bool partialMatch, TMakeNode* result) {
    Stats.Inc(partialMatch
        ? NStats::EJsonCacheStats::PartialMatchRequests
        : NStats::EJsonCacheStats::FullMatchRequests);
    NCache::TConversionContext context(Names);

    TMakeNodeSavedState::TCacheId cacheId;
    context.Convert(id, cacheId.Id);
    cacheId.StrictInputs = Conf.DumpInputsInJSON;

    const auto& matchMap = partialMatch? PartialMatchMap : FullMatchMap;

    auto restoredIt = matchMap.find(cacheId);
    if (restoredIt == matchMap.end()) {
        return false;
    }

    Stats.Inc(partialMatch
        ? NStats::EJsonCacheStats::PartialMatchSuccess
        : NStats::EJsonCacheStats::FullMatchSuccess);
    TMakeNodeSavedState& node = restoredIt->second.get();
    node.Restore(context, result);
    return true;
}

void TMakePlanCache::AddRenderedNode(const TMakeNode& newNode, TStringBuf name, TStringBuf cacheUid, TStringBuf renderId) {
    Stats.Inc(NStats::EJsonCacheStats::AddedItems);
    NCache::TConversionContext context(Names);
    if (SaveToCache) {
        AddedNodes.emplace_back(newNode, name, cacheUid, renderId, Conf, context);
    }
}

void TMakePlanCache::LoadFromContext(const TString& context) {
    if (!LoadFromCache || context.Empty()) {
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

    builder.AddBlob(new TBlobSaverMemory(TBlob::Copy(nodes.Data(), nodes.Size())));
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
