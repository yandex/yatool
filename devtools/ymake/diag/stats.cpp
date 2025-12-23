#include "stats.h"
#include "trace.h"

#include <library/cpp/iterator/mapped.h>

#include <util/string/builder.h>
#include <util/string/join.h>
#include <util/generic/strbuf.h>
#include <util/generic/utility.h>
#include <util/generic/xrange.h>

namespace NStats {
    void TStatsBase::Report() const {
        auto&& range = MakeMappedRange(xrange(Size()), [this](auto index) {
            return TStringBuilder{} << NameByIndex(index) << " = "sv << Get(index) << ';';
        });
        TString message = JoinSeq(TStringBuf(" "), range);
        NEvent::TDisplayMessage msg;
        msg.SetType("Debug");
        msg.SetSub(Name);
        msg.SetMod("unimp");
        msg.SetMessage(message);
        FORCE_TRACE(U, msg);
    }

    void TStatsBase::MonEvent(const TString& indexName, ui64 value) {
        NEvent::TMonitoringStat event;
        event.SetName(indexName);
        event.SetType("int");
        event.SetIntValue(value);
        FORCE_TRACE(U, event);
    }

    void TStatsBase::MonEvent(const TString& indexName, double value) {
        NEvent::TMonitoringStat event;
        event.SetName(indexName);
        event.SetType("double");
        event.SetDoubleValue(value);
        FORCE_TRACE(U, event);
    }

    void TStatsBase::MonEvent(const TString& indexName, bool value) {
        NEvent::TMonitoringStat event;
        event.SetName(indexName);
        event.SetType("bool");
        event.SetBoolValue(value);
        FORCE_TRACE(U, event);
    }

    template<>
    void TStats<EModulesStats>::Report() const {
        TStatsBase::Report();

        NEvent::TModulesStats event;
        event.SetAccessed(Get(EModulesStats::Accessed));
        event.SetLoaded(Get(EModulesStats::Loaded));
        event.SetOutdated(Get(EModulesStats::Outdated));
        event.SetParsed(Get(EModulesStats::Parsed));
        event.SetTotal(Get(EModulesStats::Total));
        FORCE_TRACE(U, event);

        INT_MON_EVENT(EModulesStats::Accessed);
        INT_MON_EVENT(EModulesStats::Loaded);
        INT_MON_EVENT(EModulesStats::Outdated);
        INT_MON_EVENT(EModulesStats::Parsed);
    }

    template<>
    void TStats<EMakeCommandStats>::Report() const {
        TStatsBase::Report();

        NEvent::TMakeCommandStats event;
        event.SetInitModuleEnvCalls(Get(EMakeCommandStats::InitModuleEnvCalls));
        event.SetInitModuleEnv(Get(EMakeCommandStats::InitModuleEnv));
        FORCE_TRACE(U, event);
    }

    template<>
    void TStats<EUpdIterStats>::Report() const {
        TStatsBase::Report();

        NEvent::TUpdIterStats event;
        event.SetNukedDir(Get(EUpdIterStats::NukedDir));
        FORCE_TRACE(U, event);
    }

    template<>
    void TStats<EResolveStats>::Report() const {
        TStatsBase::Report();

        NEvent::TResolveStats event;
        event.SetIncludesAttempted(Get(EResolveStats::IncludesAttempted));
        event.SetIncludesFromCache(Get(EResolveStats::IncludesFromCache));
        event.SetResolveAsKnownTotal(Get(EResolveStats::ResolveAsKnownTotal));
        event.SetResolveAsKnownFromCache(Get(EResolveStats::ResolveAsKnownFromCache));
        FORCE_TRACE(U, event);
    }

    template<>
    void TStats<EGeneralParserStats>::Report() const {
        TStatsBase::Report();

        NEvent::TGeneralParserStats event;
        event.SetCount(Get(EGeneralParserStats::Count));
        event.SetIncludes(Get(EGeneralParserStats::Includes));
        event.SetUniqueCount(Get(EGeneralParserStats::UniqueCount));
        event.SetSize(Get(EGeneralParserStats::Size));
        event.SetUniqueSize(Get(EGeneralParserStats::UniqueSize));
        FORCE_TRACE(U, event);
    }

    template<>
    void TStats<EIncParserManagerStats>::Report() const {
        TStatsBase::Report();

        NEvent::TIncParserManagerStats event;
        event.SetParseTime(Get(EIncParserManagerStats::ParseTime));
        event.SetParsedFilesCount(Get(EIncParserManagerStats::ParsedFilesCount));
        event.SetParsedFilesSize(Get(EIncParserManagerStats::ParsedFilesSize));
        event.SetParsedFilesRecovered(Get(EIncParserManagerStats::ParsedFilesRecovered));
        event.SetInFilesCount(Get(EIncParserManagerStats::InFilesCount));
        event.SetInFilesSize(Get(EIncParserManagerStats::InFilesSize));
        FORCE_TRACE(U, event);
    }

    template<>
    void TStats<EFileConfStats>::Report() const {
        TStatsBase::Report();

        NEvent::TFileConfStats event;
        event.SetLoadedSize(Get(EFileConfStats::LoadedSize));
        event.SetLoadTime(Get(EFileConfStats::LoadTime));
        event.SetLoadedMD5Time(Get(EFileConfStats::LoadedMD5Time));
        event.SetMaxLoadedMD5Time(Get(EFileConfStats::MaxLoadedMD5Time));
        event.SetLoadedCount(Get(EFileConfStats::LoadedCount));
        event.SetMaxLoadTime(Get(EFileConfStats::MaxLoadTime));
        event.SetMappedSize(Get(EFileConfStats::MappedSize));
        event.SetMappedMD5Time(Get(EFileConfStats::MappedMD5Time));
        event.SetMaxMappedMD5Time(Get(EFileConfStats::MaxMappedMD5Time));
        event.SetMappedCount(Get(EFileConfStats::MappedCount));
        event.SetMapTime(Get(EFileConfStats::MapTime));
        event.SetFromPatchCount(Get(EFileConfStats::FromPatchCount));
        event.SetFromPatchSize(Get(EFileConfStats::FromPatchSize));
        event.SetFileStatCount(Get(EFileConfStats::FileStatCount));
        event.SetLstatCount(Get(EFileConfStats::LstatCount));
        event.SetLstatSumUs(Get(EFileConfStats::LstatSumUs));
        event.SetLstatMinUs(Get(EFileConfStats::LstatMinUs));
        event.SetLstatAvrUs(Get(EFileConfStats::LstatAvrUs));
        event.SetLstatMaxUs(Get(EFileConfStats::LstatMaxUs));
        event.SetOpendirCount(Get(EFileConfStats::OpendirCount));
        event.SetOpendirSumUs(Get(EFileConfStats::OpendirSumUs));
        event.SetOpendirMinUs(Get(EFileConfStats::OpendirMinUs));
        event.SetOpendirAvrUs(Get(EFileConfStats::OpendirAvrUs));
        event.SetOpendirMaxUs(Get(EFileConfStats::OpendirMaxUs));
        event.SetReaddirCount(Get(EFileConfStats::ReaddirCount));
        event.SetReaddirSumUs(Get(EFileConfStats::ReaddirSumUs));
        event.SetReaddirMinUs(Get(EFileConfStats::ReaddirMinUs));
        event.SetReaddirAvrUs(Get(EFileConfStats::ReaddirAvrUs));
        event.SetReaddirMaxUs(Get(EFileConfStats::ReaddirMaxUs));
        event.SetListDirSumUs(Get(EFileConfStats::ListDirSumUs));
        event.SetLstatListDirSumUs(Get(EFileConfStats::LstatListDirSumUs));
        FORCE_TRACE(U, event);

        INT_MON_EVENT(EFileConfStats::LoadedSize);
        INT_MON_EVENT(EFileConfStats::LoadTime);
        INT_MON_EVENT(EFileConfStats::LoadedCount);
        INT_MON_EVENT(EFileConfStats::MappedSize);
        INT_MON_EVENT(EFileConfStats::MapTime);
        INT_MON_EVENT(EFileConfStats::MappedCount);
        MonEvent("EFileConfStats::Count", Get(EFileConfStats::LoadedCount) + Get(EFileConfStats::MappedCount));
    }

    template<>
    void TStats<EFileConfSubStats>::Report() const {
        TStatsBase::Report();

        NEvent::TFileConfSubStats event;
        event.SetBucketId(Get(EFileConfSubStats::BucketId));
        event.SetLoadedSize(Get(EFileConfSubStats::LoadedSize));
        event.SetLoadTime(Get(EFileConfSubStats::LoadTime));
        event.SetLoadedCount(Get(EFileConfSubStats::LoadedCount));
        event.SetMaxLoadTime(Get(EFileConfSubStats::MaxLoadTime));
        FORCE_TRACE(U, event);
    }

    template<>
    void TStats<EDepGraphStats>::Report() const {
        TStatsBase::Report();

        NEvent::TDepGraphStats event;
        event.SetNodesCount(Get(EDepGraphStats::NodesCount));
        event.SetEdgesCount(Get(EDepGraphStats::EdgesCount));
        event.SetFilesCount(Get(EDepGraphStats::FilesCount));
        event.SetCommandsCount(Get(EDepGraphStats::CommandsCount));
        FORCE_TRACE(U, event);
    }

    template<>
    void TStats<EInternalCacheSaverStats>::Report() const {
        TStatsBase::Report();

        NEvent::TInternalCacheSaverStats event;
        event.SetTotalCacheSize(Get(EInternalCacheSaverStats::TotalCacheSize));
        event.SetDiagnosticsCacheSize(Get(EInternalCacheSaverStats::DiagnosticsCacheSize));
        event.SetGraphCacheSize(Get(EInternalCacheSaverStats::GraphCacheSize));
        event.SetParsersCacheSize(Get(EInternalCacheSaverStats::ParsersCacheSize));
        event.SetModulesTableSize(Get(EInternalCacheSaverStats::ModulesTableSize));
        event.SetTimesTableSize(Get(EInternalCacheSaverStats::TimesTableSize));
        event.SetNamesTableSize(Get(EInternalCacheSaverStats::NamesTableSize));
        event.SetCommandsSize(Get(EInternalCacheSaverStats::CommandsSize));
        FORCE_TRACE(U, event);
    }

    template<>
    void TStats<EJsonCacheStats>::Report() const {
        TStatsBase::Report();

        NEvent::TJsonCacheStats event;
        event.SetLoadedItems(Get(EJsonCacheStats::LoadedItems));
        event.SetAddedItems(Get(EJsonCacheStats::AddedItems));
        event.SetOldItemsSaved(Get(EJsonCacheStats::OldItemsSaved));
        event.SetNewItemsSaved(Get(EJsonCacheStats::NewItemsSaved));
        event.SetTotalItemsSaved(Get(EJsonCacheStats::TotalItemsSaved));
        event.SetFullMatchLoadedItems(Get(EJsonCacheStats::FullMatchLoadedItems));
        event.SetFullMatchRequests(Get(EJsonCacheStats::FullMatchRequests));
        event.SetFullMatchSuccess(Get(EJsonCacheStats::FullMatchSuccess));
        event.SetPartialMatchLoadedItems(Get(EJsonCacheStats::PartialMatchLoadedItems));
        event.SetPartialMatchRequests(Get(EJsonCacheStats::PartialMatchRequests));
        event.SetPartialMatchSuccess(Get(EJsonCacheStats::PartialMatchSuccess));
        event.SetFullyRendered(Get(EJsonCacheStats::FullyRendered));
        event.SetPartiallyRendered(Get(EJsonCacheStats::PartiallyRendered));
        event.SetNoRendered(Get(EJsonCacheStats::NoRendered));
        FORCE_TRACE(U, event);
    }

    template<>
    void TStats<EUidsCacheStats>::Report() const {
        TStatsBase::Report();

        NEvent::TUidsCacheStats event;
        event.SetLoadedNodes(Get(EUidsCacheStats::LoadedNodes));
        event.SetSkippedNodes(Get(EUidsCacheStats::SkippedNodes));
        event.SetDiscardedNodes(Get(EUidsCacheStats::DiscardedNodes));
        event.SetLoadedLoops(Get(EUidsCacheStats::LoadedLoops));
        event.SetSkippedLoops(Get(EUidsCacheStats::SkippedLoops));
        event.SetDiscardedLoops(Get(EUidsCacheStats::DiscardedLoops));
        event.SetSavedNodes(Get(EUidsCacheStats::SavedNodes));
        event.SetSavedLoops(Get(EUidsCacheStats::SkippedLoops));
        FORCE_TRACE(U, event);
    }
}
