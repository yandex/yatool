#include "saveload.h"
#include <cstdlib>

#include "graph_changes_predictor.h"
#include "ymake.h"

#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/trace.h>

#include <library/cpp/json/json_reader.h>
#include <library/cpp/svnversion/svnversion.h>
#include <library/cpp/zipatch/reader.h>

#include <util/generic/algorithm.h>
#include <util/generic/scope.h>
#include <util/generic/guid.h>
#include <util/generic/maybe.h>
#include <util/generic/size_literals.h>
#include <util/generic/yexception.h>
#include <util/random/random.h>
#include <util/system/execpath.h>
#include <util/system/fstat.h>

namespace {
    const ui64 ImageVersion = 44;

    template <size_t HashSize>
    class TVersionImpl {
    public:
        TVersionImpl(TStringBuf hash = TStringBuf(), int timestamp = 0) {
            SetHash(hash);
            SetTimestamp(timestamp);
        }

        void SetHash(TStringBuf hash) {
            const auto size = Min(hash.size(), HashSize);
            Copy(hash.begin(), hash.begin() + size, std::begin(Hash));
            Fill(std::begin(Hash) + size, std::end(Hash), 0);
        }

        void SetTimestamp(int timestamp) {
            Timestamp = timestamp;
        }

        bool operator==(const TVersionImpl& to) const {
            return Timestamp == to.Timestamp && TStringBuf(Hash, HashSize) == TStringBuf(to.Hash, HashSize);
        }

        bool operator!=(const TVersionImpl& to) const {
            return !(*this == to);
        }
    private:
        char Hash[HashSize];
        int Timestamp;
    };

    using TVersion = TVersionImpl<40>;

    TVersion VersionId() {
        return TVersion{GetProgramCommitId(), GetProgramBuildTimestamp()};
    }

    class TZipatchChanges: public IChanges {
        struct TChangeInfo {
            TString Path;
            ui32 BlobIndex;
            EChangeType Type;
        };
    public:
        TZipatchChanges(const TFsPath& path, bool readFileContentFromZipatch)
            : Path(path), ReadFileContentFromZipatch(readFileContentFromZipatch)
        {
            Blobs.emplace_back();
        }

        bool IsContentSupported() const override {
            return ReadFileContentFromZipatch;
        }

        void Walk(IChanges::TOnChange visitor) override {
            for (const auto& item : Changes) {
                auto& path = item.Path;
                auto index = item.BlobIndex;
                auto type = item.Type;
                TStringBuf content;
                if (index != 0) {
                    Y_ASSERT(index < Blobs.size());
                    auto& blob = Blobs[index];
                    content = TStringBuf(static_cast<const char*>(blob.Data()), blob.Size());
                }
                visitor(IChanges::TChange{path, content, type});
            }
        }

        std::pair<TStringBuf, EFileKind> GetFileContent(TStringBuf fileName) const override {
            if (auto iter = BlobMap.find(fileName); iter != BlobMap.end()) {
                auto index = iter->second;
                if (index > 0) {
                    Y_ASSERT(index < Blobs.size());
                    auto& blob = Blobs[index];
                    auto content = TStringBuf(static_cast<const char*>(blob.Data()), blob.Size());
                    return {content, EFK_File};
                }
                Y_ASSERT(index == 0);
                return {TStringBuf(), EFK_Dir};
            }
            return {TStringBuf(), EFK_NotFound};
        }

        bool IsRemoved(TStringBuf path) const override {
            auto iter = LowerBound(Removed.begin(), Removed.end(), path);
            if (iter != Removed.end() && path.StartsWith(*iter)) {
                const auto size = iter->size();
                if (size == path.size() || path[size] == NPath::PATH_SEP) {
                    return true;
                }
            }
            return false;
        }

        void Read() {
            NZipatch::TReader reader(Path);

            auto handleEntry = [&](const NZipatch::TReader::TEvent& event) {
                switch (event.Action) {
                    case NZipatch::TReader::MkDir:
                        Changes.push_back(TChangeInfo{TString(event.Path), 0, EChangeType::Create});
                        if (auto [_, ok] = BlobMap.emplace(event.Path, 0); !ok) {
                            ythrow yexception() << "file [" << event.Path << "] have been already seen in zipatch.";
                        }
                        break;
                    case NZipatch::TReader::Remove:
                        Changes.push_back(TChangeInfo{TString(event.Path), 0, EChangeType::Remove});
                        Removed.push_back(Changes.back().Path);
                        break;
                    case NZipatch::TReader::StoreFile: {
                        ui32 index = 0;
                        if (ReadFileContentFromZipatch) {
                            index = Blobs.size();
                            Blobs.emplace_back();
                            TBlob::Copy(event.Data.data(), event.Data.size()).Swap(Blobs.back());
                            if (auto [_, ok] = BlobMap.emplace(event.Path, index); !ok) {
                                ythrow yexception() << "file [" << event.Path << "] have been already seen in zipatch.";
                            }
                        }
                        Changes.push_back(TChangeInfo{TString(event.Path), index, EChangeType::Create});
                        break;
                    }
                    case NZipatch::TReader::Copy:
                    case NZipatch::TReader::Move:
                        break;
                    case NZipatch::TReader::Unknown:
                        ythrow yexception() << "unknown action " << (int) event.Action;
                }
            };

            reader.Enumerate(handleEntry);

            Sort(Removed);
        }

    private:
        TFsPath Path;
        TVector<TChangeInfo> Changes;
        THashMap<TStringBuf, ui32> BlobMap;
        TVector<TBlob> Blobs;
        TVector<TStringBuf> Removed;
        bool ReadFileContentFromZipatch = false;
    };

    THolder<IChanges> ReadZipatch(const TFsPath& path, bool readFileContentFromZipatch) {
        auto changes = MakeHolder<TZipatchChanges>(path, readFileContentFromZipatch);
        changes->Read();
        return changes;
    }

    EChangeType GetChangeType(TStringBuf type) {
        if (type == "modified") {
            return EChangeType::Modify;
        } else if (type == "new_file") {
            return EChangeType::Create;
        }
        return EChangeType::Remove;
    }

    class TArcChangeListChanges: public IChanges {
    public:
        TArcChangeListChanges(const TFsPath& path) : Path(path) {
        }

        bool IsContentSupported() const override {
            return false;
        }

        void Walk(IChanges::TOnChange visitor) override {
            for (const auto& change : Changes) {
                visitor(IChanges::TChange{change.first, TStringBuf(), change.second});
            }
        }

        void Read() {
            Changes.clear();

            TFileInput jsonFile(Path);
            NJson::TJsonValue parsedJson;
            if(!NJson::ReadJsonTree(&jsonFile, &parsedJson, false)) {
                throw yexception() << " Unable to read json tree from " << Path;
            }

            auto changesArray = parsedJson.GetArraySafe().front().GetMapSafe().at("names").GetArraySafe();
            for (const auto& element : changesArray) {
                auto change = element.GetMapSafe();
                auto changePath = change.at("path").GetStringSafe();
                auto changeType = GetChangeType(change.at("status").GetStringSafe());
                Changes.emplace_back(changePath, changeType);
            }
        }
    private:
        TFsPath Path;
        TVector<std::pair<TString, EChangeType>> Changes;
    };

    THolder<IChanges> ReadArcChangeList(const TFsPath& path) {
        auto changes = MakeHolder<TArcChangeListChanges>(path);
        changes->Read();
        return changes;
    }

    THolder<IChanges> GetChanges(const TFsPath& path, bool readFileContentFromZipatch) {
        YDIAG(FU) << "Read changelist from: " << path << Endl;
        THolder<IChanges> changes = nullptr;

        try {
            auto extension = path.GetExtension();
            extension.to_lower();
            if (extension == "cl") {
                changes.Reset(ReadArcChangeList(path));
            } else if (extension == "zipatch") {
                changes.Reset(ReadZipatch(path, readFileContentFromZipatch));
            } else {
                ythrow yexception() << "unknown changelist extension " << extension;
            }
        } catch (yexception& e) {
            YConfErr(UserErr) << "Changelist file \"" << path << "\" can't be parsed (" << e.AsStrBuf() << "), ignore it" << Endl;
        }

        return changes;
    }

    class TInternalCacheSaver {
        TYMake& YMake;
        TCacheFileWriter& Writer;
        NStats::TInternalCacheSaverStats Stats{"Internal cache stats"};

        bool SaveFsCacheOnly;
        TSymbols CompactSymbols;

    public:
        TInternalCacheSaver(TYMake& yMake, TCacheFileWriter& writer)
            : YMake(yMake)
            , Writer(writer)
            , SaveFsCacheOnly(YMake.Conf.WriteFsCache && !YMake.Conf.WriteDepsCache)
            , CompactSymbols(YMake.Conf, YMake.Conf, YMake.TimeStamps)
        {}

        TFsPath Save(bool delayedWrite) {
            TString modulesData;

            if (!SaveFsCacheOnly) {
                PrepareModules(modulesData);  // This writes to Names: don't move down
            }

            SaveSymbolsTable();
            SaveTimesTable();
            SaveParsersCache();

            if (!SaveFsCacheOnly) {
                SaveModules(modulesData);
                SaveCommands();
                SaveInternalGraph();
                SaveBlacklistHash();
                SaveIsolatedProjectsHash();
                SaveDiagnostics();
                YMake.SaveStartDirs(Writer);
            }

            Stats.Set(NStats::EInternalCacheSaverStats::TotalCacheSize, Writer.GetBuilder().GetLength());
            Stats.Report();

            YDebug() << "FS cache has been saved..." << Endl;
            if (!SaveFsCacheOnly) {
                YDebug() << "Deps cache has been saved..." << Endl;
            }

            return Writer.Flush(delayedWrite);
        }

    private:
        size_t GetLastSavedSize() {
            return Writer.GetBuilder().GetBlobs().back()->GetLength();
        }

        void SaveDiagnostics() {
            auto confBuilder = MakeHolder<TMultiBlobBuilder>();
            ConfMsgManager()->Save(*confBuilder);
            Writer.AddBlob(confBuilder.Release());
            Stats.Set(NStats::EInternalCacheSaverStats::DiagnosticsCacheSize, GetLastSavedSize());
        }

        void SaveInternalGraph() {
            auto namesBuilder = MakeHolder<TMultiBlobBuilder>();
            YMake.Graph.Save(*namesBuilder);
            Writer.AddBlob(namesBuilder.Release());
            Stats.Set(NStats::EInternalCacheSaverStats::GraphCacheSize, GetLastSavedSize());
        }

        void SaveBlacklistHash() {
            Writer.AddBlob(new TBlobSaverMemory(YMake.Conf.YmakeBlacklistHash.RawData, sizeof(TMd5Sig::RawData)));
        }

        void SaveIsolatedProjectsHash() {
            Writer.AddBlob(new TBlobSaverMemory(YMake.Conf.YmakeIsolatedProjectsHash.RawData, sizeof(TMd5Sig::RawData)));
        }

        void SaveCommands() {
            auto commandsBuilder = MakeHolder<TMultiBlobBuilder>();
            YMake.Commands.Save(*commandsBuilder);
            Writer.AddBlob(commandsBuilder.Release());
            Stats.Set(NStats::EInternalCacheSaverStats::CommandsSize, GetLastSavedSize());
        }

        void SaveModules(const TString& modulesData) {
            Writer.AddBlob(new TBlobSaverMemory(modulesData.data(), modulesData.size()));
            Stats.Set(NStats::EInternalCacheSaverStats::ModulesTableSize, GetLastSavedSize());
        }

        void SaveTimesTable() {
            YMake.TimeStamps.Save(Writer.GetBuilder());
            Stats.Set(NStats::EInternalCacheSaverStats::TimesTableSize, GetLastSavedSize());
        }

        void SaveSymbolsTable() {
            auto namesBuilder = MakeHolder<TMultiBlobBuilder>();
            if (!SaveFsCacheOnly) {
                YMake.Names.Save(*namesBuilder);
            } else {
                YMake.Names.FileConf.CopySourceFilesInto(CompactSymbols.FileConf);
                CompactSymbols.Save(*namesBuilder);
            }
            Writer.AddBlob(namesBuilder.Release());
            Stats.Set(NStats::EInternalCacheSaverStats::NamesTableSize, GetLastSavedSize());
        }

        void SaveParsersCache() {
            auto parsesCacheBuilder = MakeHolder<TMultiBlobBuilder>();
            if (SaveFsCacheOnly) {
                // Symbols table was compacted, we have to fix fileIds
                YMake.IncParserManager.Cache.RemapKeys([this](ui64 oldId) {
                    auto fileId = NParsersCache::GetFileIdFromResultId(oldId);
                    TFileView name = YMake.Names.FileConf.GetName(fileId);
                    fileId = CompactSymbols.FileConf.Add(name.GetTargetStr());
                    return NParsersCache::GetResultId(NParsersCache::GetParserIdFromResultId(oldId), fileId);
                });
            }
            YMake.IncParserManager.Cache.Save(*parsesCacheBuilder);
            Writer.AddBlob(parsesCacheBuilder.Release());
            Stats.Set(NStats::EInternalCacheSaverStats::ParsersCacheSize, GetLastSavedSize());
        }

        void PrepareModules(TString& modulesData) {
            TStringOutput modulesOutput(modulesData);
            YMake.Modules.Save(&modulesOutput);
            modulesOutput.Finish();
        }
    };

    void ArcChangesEvent(bool value) {
        NEvent::TArcChanges ev;
        ev.SetHasChangelist(value);
        FORCE_TRACE(U, ev);
    }
}

TFsPath MakeTempFilename(const TString& basePath) {
    return TFsPath(ToString(basePath) + ".new" + ToString(RandomNumber<size_t>()));
}

TMd5Sig DefaultConfHash(const TBuildConfiguration& conf) {
    return conf.YmakeConfMD5;
}

TMd5Sig ExtraConfHash(const TBuildConfiguration& conf) {
    return conf.YmakeExtraConfMD5;
}

TCacheFileReader::TCacheFileReader(const TBuildConfiguration& conf, bool forceLoad, bool useExtraConf, TConfHash confHash, TConfHash extraConfHash)
    : Conf(conf)
    , ForceLoad(forceLoad)
    , UseExtraConf(useExtraConf)
    , Hash(confHash)
    , ExtraHash(extraConfHash)
{
}

TCacheFileReader::EReadResult TCacheFileReader::Read(const TFsPath& path) {
    auto blob = TBlob::FromFileContent(path);

    try {
        SubBlobs.Reset(new TSubBlobs(blob));
    } catch (...) {
        YErr() << "bummer(" << CurrentExceptionMessage() << "), graph will be rebuilt..." << Endl;
        return EReadResult::Exception;
    }

    TSubBlobs& dgBlobs = *SubBlobs;

    SubBlobsIt = dgBlobs.begin();
    return CheckVersionInfo();
}

TBlob& TCacheFileReader::GetNextBlob() {
    Y_ENSURE(SubBlobsIt != SubBlobs.Get()->end());
    return *SubBlobsIt++;
}

bool TCacheFileReader::HasNextBlob() const {
    return SubBlobsIt != SubBlobs.Get()->end();
}

TCacheFileReader::EReadResult TCacheFileReader::CheckVersionInfo() {
    TSubBlobs& blobs = *SubBlobs.Get();

    // ImageVersion, binary version and hash(config)
    if (blobs.size() < 4) {
        return EReadResult::IncompatibleFormat;
    }

    TBlob& oldImageVersion = GetNextBlob();
    TBlob& oldVersionIdBlob = GetNextBlob();
    TBlob& oldConfSignBlob = GetNextBlob();
    TBlob& oldExtraConfSignBlob = GetNextBlob();

    if (oldImageVersion.Size() != sizeof(ui64) || *(ui64*)oldImageVersion.Begin() != ImageVersion) {
        return EReadResult::IncompatibleFormat;
    }

    auto status = EReadResult::Success;

    TVersion oldVersion{};
    if (sizeof(oldVersion) != oldVersionIdBlob.Size()) {
        return EReadResult::IncompatibleFormat;
    }
    memcpy(&oldVersion, oldVersionIdBlob.Begin(), oldVersionIdBlob.Size());
    TVersion newVersion = VersionId();
    if (oldVersion != newVersion && !Conf.NoChkYMakeChg) {
        if (!ForceLoad) {
            return EReadResult::UpdatedBinary;
        }
        status = EReadResult::UpdatedBinary;
    }

    TMd5Sig confMd5;
    if (oldConfSignBlob.Size() != sizeof(confMd5.RawData)) {
        return EReadResult::IncompatibleFormat;
    }

    if (!ForceLoad) {
        memcpy(confMd5.RawData, oldConfSignBlob.Begin(), oldConfSignBlob.Size());
        if (Hash(Conf) != confMd5) {
            return EReadResult::ChangedConfig;
        }
    }

    if (UseExtraConf) {
        TMd5Sig extraConfMd5;
        memcpy(extraConfMd5.RawData, oldExtraConfSignBlob.Begin(), oldExtraConfSignBlob.Size());
        if (ExtraHash(Conf) != extraConfMd5) {
            return EReadResult::ChangedExtraConfig;
        }
    }

    return status;
}

TCacheFileWriter::TCacheFileWriter(const TBuildConfiguration& conf, const TFsPath& path, TConfHash confHash, TConfHash extraConfHash)
    : Conf(conf)
    , Path(path)
    , Hash(confHash)
    , ExtraHash(extraConfHash)
{
    SaveVersionInfo();
}

TFsPath TCacheFileWriter::Flush(bool delayed) {
    TFsPath tempFile = MakeTempFilename(Path.GetPath());
    {
        TUnbufferedFileOutput fo(tempFile);
        Builder.Save(fo, static_cast<ui32>(EMF_INTERLAY));
    }
    Builder.DeleteSubBlobs(); // builder can, too, refer to original file's content

    if (!delayed) {
        tempFile.RenameTo(Path);
        return {};
    } else {
        return tempFile;
    }
}

void TCacheFileWriter::AddBlob(IBlobSaverBase* blob) {
    Builder.AddBlob(blob);
}

TMultiBlobBuilder& TCacheFileWriter::GetBuilder() {
    return Builder;
}

void TCacheFileWriter::SaveVersionInfo() {
    static_assert(sizeof(ImageVersion) == sizeof(ui64), "expect sizeof(ImageVersion) == sizeof(ui64)");
    Builder.AddBlob(new TBlobSaverMemory(&ImageVersion, sizeof(ui64)));

    const auto version = VersionId();
    Builder.AddBlob(new TBlobSaverMemory(TBlob::Copy(&version, sizeof(TVersion))));

    const TMd5Sig currentConfMD5 = Hash(Conf);
    Builder.AddBlob(new TBlobSaverMemory(TBlob::Copy(currentConfMD5.RawData, sizeof(currentConfMD5.RawData))));

    const TMd5Sig currentExtraConfMD5 = ExtraHash(Conf);
    Builder.AddBlob(new TBlobSaverMemory(TBlob::Copy(currentExtraConfMD5.RawData, sizeof(currentExtraConfMD5.RawData))));
}

namespace {
    struct TDebugTimer {
        inline TDebugTimer(const char* descr)
            : Descr(descr)
        {
            YDebug() << "start load " << Descr << Endl;
        }

        inline ~TDebugTimer() {
            YDebug() << "end load " << Descr << Endl;
        }

        const char* Descr;
    };
}

bool TYMake::Load(const TFsPath& file) {
    try {
        return LoadImpl(file);
    } catch (...) {
        YErr() << "can not load cache: " << CurrentExceptionMessage() << Endl;
    }

    return false;
}

bool TYMake::LoadImpl(const TFsPath& file) {
    YDebug() << "load cache from " << file << Endl;

    bool useYmakeCache = !Conf.CachePath.Empty();
    auto forceLoad = useYmakeCache || Conf.ReadFsCache && !Conf.ReadDepsCache;
    TCacheFileReader cacheReader(Conf, forceLoad, true);

    auto readResult = cacheReader.Read(file);

    auto loadFsCacheFromBlobs = [&]() {
        if (cacheReader.HasNextBlob()) {
            TDebugTimer timer("names");
            Names.Load(cacheReader.GetNextBlob());
        }
        if (cacheReader.HasNextBlob()) {
            TDebugTimer timer("time stamps");
            TimeStamps.Load(cacheReader.GetNextBlob());
        }
        if (cacheReader.HasNextBlob()) {
            TDebugTimer timer("inc parser manager");
            IncParserManager.Cache.Load(cacheReader.GetNextBlob());
        } else {
            return false;
        }
        return true;
    };

    auto loadDepsCacheFromBlobs = [&]() {
        if (cacheReader.HasNextBlob()) {
            TDebugTimer timer("modules");
            Modules.Load(cacheReader.GetNextBlob());
        }
        if (cacheReader.HasNextBlob()) {
            TDebugTimer timer("commands");
            Commands.Load(cacheReader.GetNextBlob());
        }
        if (cacheReader.HasNextBlob()) {
            TDebugTimer timer("graph");
            Graph.Load(cacheReader.GetNextBlob());
        }
        if (cacheReader.HasNextBlob()) {
            auto& blob = cacheReader.GetNextBlob();
            TMd5Sig YmakePreBlacklistHash;
            Y_ASSERT(blob.Length() == sizeof(TMd5Sig::RawData));
            memcpy(YmakePreBlacklistHash.RawData, blob.Data(), sizeof(TMd5Sig::RawData));
            Conf.SetBlacklistHashChanged(YmakePreBlacklistHash != Conf.YmakeBlacklistHash);
            if (Conf.IsBlacklistHashChanged()) {
                YDebug() << "Blacklist has changed" << Endl;
            }
        }
        if (cacheReader.HasNextBlob()) {
            auto& blob = cacheReader.GetNextBlob();
            TMd5Sig YmakePreIsolatedProjectsHash;
            Y_ASSERT(blob.Length() == sizeof(TMd5Sig::RawData));
            memcpy(YmakePreIsolatedProjectsHash.RawData, blob.Data(), sizeof(TMd5Sig::RawData));
            Conf.SetIsolatedProjectsHashChanged(YmakePreIsolatedProjectsHash != Conf.YmakeIsolatedProjectsHash);
            if (Conf.IsIsolatedProjectsHashChanged()) {
                YDebug() << "Isolated projects had changed" << Endl;
            }
        }
        if (cacheReader.HasNextBlob()) {
            TDebugTimer timer("conf msg manager");
            ConfMsgManager()->Load(cacheReader.GetNextBlob());
        }
        if (cacheReader.HasNextBlob()) {
            TDebugTimer timer("start dirs");
            TBlob blob = cacheReader.GetNextBlob();
            TMemoryInput inputStartDirs(blob.Data(), blob.Length());
            TSerializer<decltype(PrevStartDirs_)>::Load(&inputStartDirs, PrevStartDirs_);
        } else {
            return false;
        }
        return true;
    };

    bool loadFsCache = Conf.ReadFsCache;
    bool loadDepsCache = Conf.ReadDepsCache;
    const char* info = nullptr;
    switch (readResult) {
        case TCacheFileReader::Success:
            if (useYmakeCache) {
                info = "Graph is readonly loaded from cache.";
            }
            break;
        case TCacheFileReader::ChangedExtraConfig:
            if (useYmakeCache) {
                info = "Graph is readonly loaded from cache.";
            }
            HasGraphStructuralChanges_ = true;
            YDebug() << "Graph maybe has structural changes because extra conf is changed" << Endl;
            break;
        case TCacheFileReader::IncompatibleFormat:
            info = "Incompatible ymake.cache format, graph will be rebuilt...";
            loadFsCache = false;
            loadDepsCache = false;
            break;
        case TCacheFileReader::ChangedConfig:
            info = "Config has changed, graph will be rebuilt...";
            loadDepsCache = false;
            break;
        case TCacheFileReader::UpdatedBinary:
            if (forceLoad) {
                info = "Current ymake binary and cache may be not compatible.";
            } else {
                info = "Updated ymake binary, graph will be rebuilt...";
                loadDepsCache = false;
            }
            break;
        default:
            loadFsCache = false;
            loadDepsCache = false;
    }

    if (info != nullptr) {
        YInfo() << info << Endl;
    }

    TString prevDepsFingerprint;
    if (cacheReader.HasNextBlob()) {
        TBlob blob = cacheReader.GetNextBlob();
        prevDepsFingerprint = TString(reinterpret_cast<const char*>(blob.Data()), blob.Length());
    }

    if (loadFsCache && loadFsCacheFromBlobs()) {
        YDebug() << "FS cache has been loaded..." << Endl;
        FSCacheLoaded_ = true;
    } else {
        return false;
    }

    if (loadDepsCache && cacheReader.HasNextBlob()) {
        if (loadDepsCacheFromBlobs()) {
            YDebug() << "Deps cache has been loaded..." << Endl;
            PrevDepsFingerprint = prevDepsFingerprint;
            DepsCacheLoaded_ = true;
        } else {
            return false;
        }
    }

    TimeStamps.InitSession(Graph.GetFileNodeData());

    return true;
}

void TYMake::AnalyzeGraphChanges(IChanges& changes) {
    if (!DepsCacheLoaded_) {
        HasGraphStructuralChanges_ = true;
        YDebug() << "Graph has structural changes because dep cache isn't loaded" << Endl;
    }
    if (PrevStartDirs_ != CurStartDirs_) {
        HasGraphStructuralChanges_ = true;
        YDebug() << "Graph has structural changes because start dirs are different" << Endl;
    }
    if (!HasGraphStructuralChanges_ && Conf.ShouldUseGraphChangesPredictor()) {
        TGraphChangesPredictor predictor(IncParserManager, Names.FileConf, changes);
        predictor.AnalyzeChanges();
        HasGraphStructuralChanges_ = predictor.HasChanges();
    }
}

bool TYMake::LoadPatch() {
    ArcChangesEvent(!Conf.PatchPath.Empty());
    if (!Conf.PatchPath) {
        return true;
    }

    if (TimeStamps.IsNeedNewSession()) { // if cache not loaded by Load() must init session
        TimeStamps.InitSession(Graph.GetFileNodeData());
    }
    auto changes = GetChanges(Conf.PatchPath, Conf.ReadFileContentFromZipatch);
    if (changes) {
        AnalyzeGraphChanges(*changes);
        Names.FileConf.UseExternalChanges(std::move(changes));
        GraphChangesPredictionEvent();
    }
    return true;
}

void TYMake::LoadUids(TUidsCachable* cachable) {
    if (!Conf.ReadUidsCache) {
        return;
    }
    UidsCacheLoaded_ = TryLoadUids(cachable);
}

bool TYMake::TryLoadUids(TUidsCachable* cachable) {
    if (!PrevDepsFingerprint.Empty() && Conf.YmakeUidsCache.Exists()) {
        NYMake::TTraceStage loadUidsStage{"Load Uids cache"};

        TFileInput input(TFile{Conf.YmakeUidsCache, OpenExisting | RdOnly | Seq | NoReuse}, 1_MB);

        ui32 size;
        if (input.Load(&size, sizeof(size)) != sizeof(size))
            return false;

        TString depsFingerprint;
        depsFingerprint.ReserveAndResize(size);
        if (input.Load(depsFingerprint.Detach(), size) != size)
            return false;

        if (depsFingerprint != PrevDepsFingerprint) {
            YDebug() << "Uids cache fingerprint mismatch: "
                << depsFingerprint << " != " << PrevDepsFingerprint << Endl;
            return false;
        }

        cachable->LoadCache(&input, Graph);

        YDebug() << "Uids cache has been loaded..." << Endl;
        return true;
    }
    return false;
}

TVector<ui32> TYMake::PreserveStartTargets() const {
    TVector<ui32> result;
    result.reserve(StartTargets.size());
    for (const auto& target : StartTargets) {
        result.push_back(Graph[target.Id]->ElemId);
    }
    return result;
}

void TYMake::FixStartTargets(const TVector<ui32>& elemIds) {
    int i = 0;
    for (ui32 elemId : elemIds) {
        StartTargets[i++].Id = Graph.GetFileNodeById(elemId).Id();
    }
}

void TYMake::SaveStartDirs(TCacheFileWriter& writer) {
    TBuffer buffer;
    TBufferOutput output(buffer);
    TSerializer<decltype(CurStartDirs_)>::Save(&output, CurStartDirs_);
    writer.AddBlob(new TBlobSaverMemory(TBlob::FromBufferSingleThreaded(buffer)));
}

void TYMake::Save(const TFsPath& file, bool delayed) {
    // Graph.Save() requires no references into graph
    Modules.ResetTransitiveInfo();
    TVector<ui32> stIds = PreserveStartTargets();

    TCacheFileWriter cacheWriter(Conf, file);

    CurrDepsFingerprint = TGUID::Create().AsGuidString();
    cacheWriter.AddBlob(new TBlobSaverMemory(TBlob::FromStringSingleThreaded(CurrDepsFingerprint)));

    TInternalCacheSaver saver(*this, cacheWriter);
    DepCacheTempFile = saver.Save(delayed);
    if (!delayed) {
        Conf.OnDepsCacheSaved();
    }

    FixStartTargets(stIds);
    IncParserManager.Cache.Clear();
}

void TYMake::SaveUids(TUidsCachable* uidsCachable) {
    if (Conf.WriteUidsCache && !CurrDepsFingerprint.Empty()) {
        FORCE_TRACE(U, NEvent::TStageStarted("Save Uids cache"));

        UidsCacheTempFile = MakeTempFilename(Conf.YmakeUidsCache.GetPath());
        TFileOutput uidsOutput{TFile{UidsCacheTempFile.GetPath(), CreateAlways | WrOnly}};

        ui32 size = CurrDepsFingerprint.Size();
        uidsOutput.Write(&size, sizeof(size));
        uidsOutput.Write(CurrDepsFingerprint.Data(), size);

        uidsCachable->SaveCache(&uidsOutput, Graph);
        YDebug() << "Uids cache has been saved..." << Endl;

        FORCE_TRACE(U, NEvent::TStageFinished("Save Uids cache"));
    }
}

void TYMake::CommitCaches() {
    if (Conf.WriteFsCache || Conf.WriteDepsCache) {
        if (DepCacheTempFile.IsDefined()) {
            Modules.Clear();
            Names.Clear(); // This will unlock any mapped data in graph cache
            DepCacheTempFile.RenameTo(Conf.YmakeCache);
            DepCacheTempFile = {};
            Conf.OnDepsCacheSaved();
        }
    } else {
        Y_ASSERT(!DepCacheTempFile.IsDefined());
    }

    if (Conf.WriteUidsCache) {
        if (UidsCacheTempFile.IsDefined()) {
            UidsCacheTempFile.RenameTo(Conf.YmakeUidsCache);
            UidsCacheTempFile = {};
        }
    } else {
        Y_ASSERT(!UidsCacheTempFile.IsDefined());
    }
}

void TYMake::JSONCacheLoaded(bool jsonCacheLoaded) {
    JSONCacheLoaded_ = jsonCacheLoaded;

}

void TYMake::FSCacheMonEvent() const {
    if (FSCacheLoaded_) {
        NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::UsedFSCache), true); // loaded OK
    } else if (Conf.ReadFsCache) {
        NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::UsedFSCache), false); // enabled, but not loaded
    }
}

void TYMake::DepsCacheMonEvent() const {
    if (DepsCacheLoaded_) {
        NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::UsedDepsCache), true); // loaded OK
    } else if (Conf.ReadDepsCache) {
        NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::UsedDepsCache), false); // enabled, but not loaded
    }
}

void TYMake::GraphChangesPredictionEvent() const {
    NEvent::TGraphChangesPrediction ev;
    ev.SetPredictsStructuralChanges(HasGraphStructuralChanges_);
    FORCE_TRACE(U, ev);
}

void TYMake::JSONCacheMonEvent() const {
    if (JSONCacheLoaded_) {
        NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::UsedJSONCache), true); // loaded OK
    } else  if (Conf.ReadJsonCache) {
        NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::UsedJSONCache), false); // enabled, but not loaded
    }
}

void TYMake::UidsCacheMonEvent() const {
    if (UidsCacheLoaded_) {
        NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::UsedUidsCache), true); // loaded OK
    } else  if (Conf.ReadUidsCache) {
        NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::UsedUidsCache), false); // enabled, but not loaded
    }
}
