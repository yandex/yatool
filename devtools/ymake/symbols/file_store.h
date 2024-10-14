#pragma once

#include "name_data_store.h"
#include "content_provider.h"
#include "sortedreaddir.h"
#include "base2fullnamer.h"

#include <devtools/ymake/common/content_holder.h>
#include <devtools/ymake/common/md5sig.h>
#include <devtools/ymake/diag/stats.h>

#include <util/generic/hash_set.h>
#include <util/datetime/base.h>
#include <util/string/builder.h>
#include "util/charset/utf8.h"

class TTimeStamps;
class TFileConf;
struct TRootsOptions;
struct TDebugOptions;

struct TFileData {
    TFileData();

    TMd5Sig HashSum; // @nodmp
    i64 ModTime;
    ui64 Size;
    union {
        ui32 AllFlags; // @nodmp 22 bits used
        struct {
            ui8 LastCheckedStamp;
            ui8 RealModStamp;

            ui8 CheckContent : 1; // ModTime or Size changed
            ui8 NotFound     : 1;
            ui8 IsDir        : 1;
            ui8 IsSource     : 1; // $S or $U
            ui8 Changed      : 1; // filled by last CheckFS or GetContent call
            ui8 CantRead     : 1; // ReadContent operation failed
            ui8 IsMakeFile     : 1; // Make file or include from ya.make
        };
    };

    /// Consistently fill data as not found in current (by `curStamp`) session
    ///
    /// temporary means that error happened during status chech and so status
    /// should be uncacheable (via CantRead)
    const TFileData& MarkNotFound(ui8 curStamp, bool temporary = false) {
        CheckContent = false;
        Size = 0;
        ModTime = 0;
        HashSum = {};
        return FinalizeStatusUpdate(false, curStamp, temporary);
    }

    /// Consistently fill status data fields to `found` state in current (by `curStamp`) session
    ///
    /// temporary means that error happened during status chech and so status
    /// should be uncacheable (via CantRead)
    const TFileData& FinalizeStatusUpdate(bool found, ui8 curStamp, bool temporary = false) {
        bool fileStatusChanged = (NotFound == found);
        NotFound = !found;
        Changed = CheckContent || fileStatusChanged;
        if (fileStatusChanged && !found) {
            // File lost, record the time of detection
            RealModStamp = curStamp;
        }
        LastCheckedStamp = curStamp;
        CantRead = temporary && !found;
        return *this;
    }

    /// Check whether file status is up-to-date in current (by `curStamp`) session
    bool IsStatusUpToDate(ui8 curStamp) const {
        return LastCheckedStamp == curStamp;
    }

    /// Consistently fill data as unreadable in current (by `curStamp`) session
    /// Fill HashSum field with hash of the file name provided via `nameHash`
    const TFileData& MarkUnreadable(TMd5Sig nameHash, ui8 curStamp) {
        CantRead = true;
        Changed = true;
        CheckContent = false;
        HashSum = nameHash;
        RealModStamp = curStamp;
        NotFound = false;
        return *this;
    }

    /// Consistently fill readable data content fileds in current (by `curStamp`) session
    /// Fill HashSum field with hash of the file or directory content hash provided via `nameHash`
    const TFileData& FinalizeContentUpdate(TMd5Sig newHash, ui8 curStamp) {
        if (newHash != HashSum) {
            RealModStamp = curStamp;
            HashSum = newHash;
        }
        Changed = RealModStamp == curStamp;
        CheckContent = false;
        NotFound = false;
        return *this;
    }

    /// Check whether file content was updated in current (by `curStamp`) session
    ///
    /// Note: Unlike status info this check alone is not enough to declare that content info is up to date
    /// This only tells that if file or directory content changed in current session the info was updated to
    /// its current state, but for unchanged up-to-date files and directories this check doesn't hold.
    bool IsContentUpdated(ui8 curStamp) const {
        return RealModStamp == curStamp;
    }

    Y_SAVELOAD_DEFINE(HashSum, ModTime, Size, AllFlags);
};

static_assert(sizeof(TFileData) == sizeof(TMd5Sig) + sizeof(i64) + sizeof(ui64) + sizeof(ui32) + sizeof(ui32), "union part of TFileData must fit 32 bit"); // HashSum+ModTime+Size+AllFlags+padding

enum ELinkType : ui8 {
    ELT_Default = 0,
    ELT_MKF, // makefiles
    ELT_Text,
    ELT_Action,
    ELT_Module,
    ELT_COUNT
};

/// Helper functions for ELinkType
class ELinkTypeHelper {
public:
    /// Link type to name table
    static constexpr TStringBuf Type2Name[ELinkType::ELT_COUNT] = {
        ""sv,
        "MKF"sv,
        "TEXT"sv,
        "ACTION"sv,
        "MODULE"sv,
    };

    /// Link type to prefix table
    static constexpr TStringBuf Type2Prefix[ELinkType::ELT_COUNT] = {
        ""sv,
        "$L/MKF/"sv,
        "$L/TEXT/"sv,
        "$L/ACTION/"sv,
        "$L/MODULE/"sv,
    };

    /// Convert name of link to link type
    static ELinkType Name2Type(TStringBuf name);

    /// Return pair of TStringBuf: linkPrefix and targetPath. If concat they, get result path to target with link
    static std::tuple<TStringBuf, TStringBuf> LinkToTargetBufPair(ELinkType context, TStringBuf target) {
        return { Type2Prefix[context], target };
    }

    /// Return TString result path with link to target
    static TString LinkToTargetString(ELinkType context, TStringBuf target);
};

/// Combine index in NameStore and link context type ELinkType in one ui32 word
class TFileId {
public:
    TFileId()
        : TargetId(0)
        , LinkType(ELT_Default)
    {}

    bool IsLink() const {
        return LinkType != ELT_Default;
    }

    ELinkType GetLinkType() const {
        return static_cast<ELinkType>(LinkType);
    }

    ui32 GetTargetId() const {
        return TargetId;
    }

    ui32 GetElemId() const {
        return ElemId;
    }

    TStringBuf GetLinkName() const {
        return ELinkTypeHelper::Type2Name[static_cast<ELinkType>(LinkType)];
    }

    bool Empty() const {
        return ElemId == 0;
    }

public: // Creating TFileId
    static TFileId Create(ui32 elemId) {
        return TFileId(elemId);
    }

    static TFileId Create(ELinkType linkType, ui32 targetId) {
        return TFileId(linkType, targetId);
    }

    static TFileId Create(TStringBuf linkName, ui32 targetId) {
        return TFileId(linkName, targetId);
    }

public: // Creating ElemId from TargetId and LinkType
    static ui32 CreateElemId(ELinkType linkType, ui32 targetId) {
        return Create(linkType, targetId).GetElemId();
    }

    static ui32 CreateElemId(TStringBuf linkName, ui32 targetId) {
        return Create(linkName, targetId).GetElemId();
    }

    /// Magic value for mark removed elements in directory items list
    static ui32 RemovedElemId() {
        return TFileId::CreateElemId(ELT_Default, Max<ui32>());
    }

private:
    union {
        ui32 ElemId; ///< Combine TargetId and LinkType in one ui32 word
        struct {
            ui32 TargetId : 29; ///< Index in NameStore table
            ui32 LinkType : 3;  ///< Link context type (see ELinkType above)
        };
    };
    static_assert(ELT_COUNT <= (1 << 3u /* bits in LinkType */));

protected: // constructors only for use in static create functions
    explicit TFileId(ui32 elemId)
        : ElemId(elemId)
    {}

    explicit TFileId(ELinkType linkType, ui32 targetId)
        : TargetId(targetId)
        , LinkType(linkType)
    {}

    explicit TFileId(TStringBuf linkName, ui32 targetId)
        : TargetId(targetId)
        , LinkType(ELinkTypeHelper::Name2Type(linkName))
    {}
};

class TFileView : public TFileId {
private:
    const TNameStore* Table;

    friend class TFileConf;
public:
    TFileView()
        : TFileId()
        , Table(nullptr)
    {}

    TFileView(const TNameStore* table, ui32 elemId)
        : TFileId(elemId)
        , Table(table)
    {}

    void GetStr(TString& name) const;
    TStringBuf GetTargetStr() const;

    bool HasId() const {
        return true;
    }

    bool IsValid() const;
    bool operator==(const TFileView& view) const;
    std::strong_ordering operator<=>(const TFileView& view) const;

    // from NPath
    bool IsType(NPath::ERoot root) const;
    bool InSrcDir() const;
    NPath::ERoot GetType() const;
    TStringBuf CutAllTypes() const;
    TStringBuf CutType() const;
    TStringBuf Basename() const;
    TStringBuf BasenameWithoutExtension() const;
    TStringBuf Extension() const;
    TStringBuf NoExtension() const;

    bool IsLink() const;
    TStringBuf GetContextFromLink() const;
    ELinkType GetContextType() const;

    void UpdateMD5(MD5& md5) const;
};

IOutputStream& operator<<(IOutputStream& out, TFileView view);

bool operator==(TStringBuf str, TFileView view);
bool operator==(TFileView view, TStringBuf str);

template<>
struct THash<TFileView> {
    size_t operator()(const TFileView& view) const {
        return THash<ui32>()(view.GetElemId());
    }
};

enum class ECheckForChangesMethod : bool {
    PRECISE,
    RELAXED
};

class TFileContentHolder : public IContentHolder {
private:
    TFileConf& FileConf_;

    ui32 TargetId_;
    bool IsSource_;
    TString AbsoluteName_;

    TBlob Content_;
    bool ContentReady_ = false;

    friend class TFileConf;

public:
    ui32 OriginalId = 0;    // With which Id FileContent was requested (for internal links support)

public:
    ~TFileContentHolder() = default;

    TStringBuf GetAbsoluteName() override;
    TStringBuf GetContent() override;

    bool CheckForChanges(ECheckForChangesMethod checkMethod);

    const TFileData& GetFileData() const;
    TFileData& GetFileData();

    bool IsNotFound() const {
        return GetFileData().NotFound;
    }

    void UpdateContentHash();

    ELinkType GetProcessingContext() const;
    TFileView GetName() const;

    ui32 GetTargetId() const;
    bool IsInternalLink() const;

    bool WasRead() const;
    size_t Size() const;

    void ValidateUtf8(const TStringBuf fileName);

private:
    TFileContentHolder(TFileConf& fileConf, ui32 targetId, TString&& absName);

    void ReadContent();
};

using TFileHolder = THolder<TFileContentHolder>;

enum class EPathKind {
    Any,
    Dir,
    File,
    MaybeDir, // NotFound or IsDir
};

enum class EChangeType {
    Modify, // only for arc changelist
    Create,
    Remove
};

class IChanges {
public:
    struct TChange {
        TStringBuf Name;
        TStringBuf Content;
        EChangeType Type;
    };
    using TOnChange = std::function<void(const TChange&)>;

    enum EFileKind {
        EFK_NotFound,
        EFK_File,
        EFK_Dir
    };

    virtual ~IChanges() = default;
    virtual void Walk(TOnChange visitor) = 0;
    virtual bool IsContentSupported() const = 0;
    virtual std::pair<TStringBuf, EFileKind> GetFileContent(TStringBuf /* fileName */) const {
        return {TStringBuf(), EFK_NotFound};
    }
    virtual bool IsRemoved(TStringBuf /* path */) const {
        return false;
    }
};

class TFileConf: public TNameDataStore<TFileData, TFileView> {
private:
    using TBase = TNameDataStore<TFileData, TFileView>;

    bool UseExternalChangesSource = false;
    TFileView SourceDir;
    TFileView BuildDir;
    TFileView BuildDummyFile;
    THashSet<ui32> ExternalChanges;
    THashMap<ui32, TVector<ui32>> DirData;
    THolder<IChanges> Changes = nullptr;
    THashMap<ui32, THashSet<ui32>> ChangesAddedDirContent;

    mutable NStats::TFileConfStats Stats{"File access stats"};
    mutable THolder<NStats::TFileConfSubStats> SubStats = nullptr;

    enum class EMarkAsChangedResult : bool {
        DONE,
        UPDATE_PARENT
    };

private:
    friend class TFileContentHolder;
    friend class TTimeStamps;

private:
    // Statistics collection and logging
    void RecordStats(ui64 size, TInstant start, bool mapped);
    void SumUsStat(size_t us, NStats::EFileConfStats countStat, NStats::EFileConfStats sumUsStat, NStats::EFileConfStats minUsStat, NStats::EFileConfStats maxUsStat) const;
    void AvrUsStat(NStats::EFileConfStats countStat, NStats::EFileConfStats sumUsStat, NStats::EFileConfStats avrUsStat) const;
    void RecordMD5Stats(TInstant start, bool mapped);

    /// Returns system error code upon failure, 0 on success
    int FileStat(TStringBuf name, TStringBuf content, IChanges::EFileKind kind, TFileStat& result) const;

    void ReadContent(TStringBuf fileName, TFileContentHolder& contentHolder);
    void ReadContent(ui32 id, TFileContentHolder& contentHolder);
    void ReadContent(TFileView view, TString&& realPath, TFileContentHolder& contentHolder);

    const TFileData& CheckFS(ui32 elemId, bool runStat = true, const TFileStat* fileStat = nullptr);
    /// ATTN: (1) adds `name' to the namespace; (2) visits the FS only for $S names
    const TFileData& VCheckFS(TFileView name);

    EMarkAsChangedResult MarkFileAsChanged(const TString& filename);

    ui32 CopySourceFileInto(ui32 id, TFileConf& other) const;

    using TBase::GetById;

public:
    TFileConf(const TRootsOptions& configuration, const TDebugOptions& debugOptions, TTimeStamps& timestamps)
            : Configuration(configuration)
            , DebugOptions(debugOptions)
            , TimeStamps(timestamps)
    {
        Stats.Set(NStats::EFileConfStats::LstatMinUs, Max<ui64>());
        Stats.Set(NStats::EFileConfStats::OpendirMinUs, Max<ui64>());
        Stats.Set(NStats::EFileConfStats::ReaddirMinUs, Max<ui64>());
        IsChangesContentSupported_ = false;
    }

    const TRootsOptions& Configuration;
    const TDebugOptions& DebugOptions;
    TTimeStamps& TimeStamps;
    /// ATTN: this does not necessarily equals to ModTime field

    void InitAfterCacheLoading();
    ui32 Add(TStringBuf name);
    TStringBuf RetBuf(ui32 targetId) const {
        Y_ASSERT(NameStore.CheckId(targetId));
        return NameStore.GetStringBufName(targetId);
    }

    TFileView GetStoredName(TStringBuf name);
    TFileView GetName(ui32 id) const;
    TFileView GetTargetName(ui32 elemId) const;

    bool HasName(TStringBuf name) const;

    ui32 GetId(TStringBuf name) const;
    ui32 GetIdNx(TStringBuf name) const;

    const TFileData& GetFileDataById(ui32 elemId) const;
    TFileData& GetFileDataById(ui32 elemId);

    /// Return file data with updated status. Stat or don't stat FS depending on `stat` argument
    const TFileData& GetFileDataByIdWithStatusUpdate(ui32 elemId, bool stat);

    void MarkAsMakeFile(TFileView view);

    /// Get const full file metadata
    const TFileData& GetFileData(TFileView name) const;
    /// Get full file metadata
    TFileData& GetFileData(TFileView name);

    bool IsStatusUpToDate(const TFileData& data) const;
    bool IsContentUpdated(const TFileData& data) const;

    THolder<TFileContentHolder> GetFileById(ui32 elemId);
    THolder<TFileContentHolder> GetFileByName(TStringBuf name);
    THolder<TFileContentHolder> GetFileByName(TFileView name);
    THolder<TFileContentHolder> GetFileByAbsPath(TStringBuf path);

    /// Check validity file data for kind
    bool IsYPathValid(const TFileData& data, EPathKind kind);

    /// Check NeedFix of path and reconstruct it, if need by use pathStrBuf
    TStringBuf YPathFix(TStringBuf path, TString& pathStrBuf);

    /// Check exists and kind by path, if not found or invalid kind - return empty view
    TFileView YPathExists(TStringBuf path, EPathKind kind = EPathKind::Any, bool pathFixed = false);

    /// Check exists and kind by pathView, if not found or invalid kind - return false
    bool YPathExists(TFileView pathView, EPathKind kind = EPathKind::Any);

    /// Check file/dir exists in directory
    bool DirHasFile(TStringBuf dirName, TStringBuf fileName, EPathKind kind);
    /// Check file/dir exists in directory
    bool DirHasFile(TFileView dirView, TStringBuf fileName, EPathKind kind);

    /// Check ya.make file exists in directory
    bool DirHasYaMakeFile(TStringBuf dirName);
    /// Check ya.make file exists in directory
    bool DirHasYaMakeFile(TFileView dirView);

    bool CheckDirectory(TStringBuf name);
    bool CheckDirectory(TFileView name);
    bool CheckExistentDirectory(TStringBuf name);
    bool CheckExistentDirectory(TFileView name);
    bool CheckLostDirectory(TFileView name);
    bool IsNonExistedSrcDir(TStringBuf name);

    /// Return <removed, content, kind> from Changes
    std::tuple<bool, TStringBuf, IChanges::EFileKind> GetFileChanges(TStringBuf name) const;
    /// Return <found, removedOrInvalidKind> from Changes, if content supported in Changes
    std::tuple<bool, bool> FindFileInChangesNoFS(TStringBuf name, bool allowFile, bool allowDir) const;

    // ATTN! Will invalidate any other TFileData refs and pointers
    void ListDir(ui32 dirElemId, bool forceList = false, bool forceStat = false);

    const TVector<ui32>& GetCachedDirContent(ui32 dirId) const {
        return DirData.at(dirId);
    }

    void UseExternalChanges(THolder<IChanges> changes);

    const NStats::TFileConfStats& StatsRef() const noexcept {
        return Stats;
    }
    void ReportStats() const;

    void CopySourceFilesInto(TFileConf& other) const;

    //from NPath functions
    bool IsPrefixOf(TFileView view, TStringBuf path) const;

    TFileView SrcDir() const;
    TFileView BldDir() const;
    TFileView DummyFile() const;
    TFileView Parent(TFileView view);
    TFileView ReplaceRoot(TFileView view, NPath::ERoot root);
    TFileView CreateFile(TFileView dir, TStringBuf filename);

    //link functions
    TFileView ResolveLink(TFileView view) const;
    TFileView ResolveLink(ui32 id) const;

    const THashSet<ui32>& GetExternalChanges() const {
        return ExternalChanges;
    }

    static TFileView ConstructLink(ELinkType context, TFileView target);
    static TString ConstructLink(ELinkType context, TStringBuf target);
    static TString ConstructPathWithLink(ELinkType context, TStringBuf target, NPath::ERoot root);
    static bool IsLink(ui32 elemId);
    static ui32 GetTargetId(ui32 elemId);
    static TStringBuf GetContextStr(ui32 elemId);
    static ELinkType GetContextType(TStringBuf context);

private:
    TSortedReadDir SortedReadDir_;
    TBase2FullNamer Base2FullNamer_;
    bool IsChangesContentSupported_;
};


class TCachedFileConfContentProvider: public IContentProvider {
private:
    using TCacheStorage = absl::flat_hash_map<ui32, THolder<TFileContentHolder>>;

    TFileConf& Conf_;
    TCacheStorage Storage_;

    size_t ReadCount_ = 0;
    ui64 ReadSize_ = 0;
    size_t FillCount_ = 0;
    ui64 FillSize_ = 0;

public:
    explicit TCachedFileConfContentProvider(TFileConf& conf)
        : Conf_(conf)
    {
    }

    size_t UniqCount() const {
        return FillCount_;
    }

    ui64 UniqSize() const {
        return FillSize_;
    }

    size_t ReadCount() const {
        return ReadCount_;
    }

    ui64 ReadSize() const {
        return ReadSize_;
    }

    // Having this in header mystically gives 25% of perf in my experiments
    TStringBuf Content(TStringBuf path) override {
        TFileView file = Conf_.GetStoredName(path);
        ui32 id = file.GetElemId();
        ++ReadCount_;

        if (Storage_.contains(id)) {
            const auto& cached = Storage_[id];
            ReadSize_ += cached->Size();
            return cached->GetContent();
        } else {
            THolder<TFileContentHolder> holder = Conf_.GetFileByName(file);
            auto content = holder->GetContent();
            Y_ASSERT(holder->WasRead());
            const auto& fileData = holder->GetFileData();
            if (!fileData.CantRead) {
                holder->ValidateUtf8(file.GetTargetStr());
                ReadSize_ += holder->Size();
                FillSize_ += holder->Size();
                ++FillCount_;
                Storage_[id].Swap(holder);
            } else {
                throw yexception() << "File '" << file << "' is unreadable";
            }
            return content;
        }
    }
};
