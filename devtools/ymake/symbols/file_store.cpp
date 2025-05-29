#include "file_store.h"
#include "time_store.h"

#include <devtools/ymake/diag/stats.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag/progress_manager.h>
#include <devtools/ymake/common/cyclestimer.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/options/debug_options.h>
#include <devtools/ymake/options/roots_options.h>

#include <library/cpp/digest/crc32c/crc32c.h>
#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/retry/retry.h>

#include <util/digest/city.h>
#include <util/generic/algorithm.h>
#include <util/memory/blob.h>
#include <util/folder/dirut.h>
#include <util/folder/iterator.h>
#include <util/string/builder.h>
#include <util/string/vector.h>
#include <util/system/datetime.h>
#include <util/system/error.h>
#include <util/system/hp_timer.h>
#include <util/system/yassert.h>

namespace {
    /// Can we treat negative result from FileStat as actual file absence
    /// or just something gone wrong
    bool CanTrustStatError(int err) {
        Y_ASSERT(err);

        // Do not reorder: codes are listed from most probable to optimize fast path
#ifdef _win_
        return err == ERROR_FILE_NOT_FOUND ||
               err == ERROR_PATH_NOT_FOUND ||
               err == ERROR_BAD_NETPATH ||
               err == ERROR_INVALID_NAME ||
               err == WSAENAMETOOLONG ||
               err == ERROR_ACCESS_DENIED ||
               err == ERROR_NOACCESS ||
               err == ERROR_INVALID_ACCESS ||
               err == WSAEACCES;
#else
        return err == ENOENT ||
               err == ENOTDIR ||
               err == ENAMETOOLONG ||
               err == EACCES ||
               err == EPERM;
#endif
    }
}


ELinkType ELinkTypeHelper::Name2Type(TStringBuf name) {
    if (name.empty()) { // fast way for no link
        return ELT_Default;
    }
    // For 2 elements array compare in circle must be faster then THashMap
    for (
        auto linkType = static_cast<ELinkType>(ELT_Default + 1);
        linkType < ELT_COUNT;
        linkType = static_cast<ELinkType>(linkType + 1)
    ) {
        if (Type2Name[linkType] == name) {
            return linkType;
        }
    }
    throw yexception() << "Don't exist context with " << name << " name ";
    Y_UNREACHABLE();
}

TString ELinkTypeHelper::LinkToTargetString(ELinkType context, TStringBuf target) {
    TStringBuf linkPrefix = Type2Prefix[context];
    return linkPrefix.empty()
        ? TString(target)
        : TStringBuilder() << linkPrefix << target;
}

TFileData::TFileData()
    : ModTime(0)
    , Size(0)
    , AllFlags(0)
{
    LastCheckedStamp = TTimeStamps::Never;
}


void TFileConf::InitAfterCacheLoading() {
    SourceDir = GetStoredName(NPath::SrcDir());
    BuildDir = GetStoredName(NPath::BldDir());
    BuildDummyFile = GetStoredName(NPath::DummyFile());
}

ui32 TFileConf::Add(TStringBuf name) {
    NPath::ValidateEx(name);
    bool isLink = NPath::IsLink(name);
    TStringBuf context;
    if (isLink) {
        context = NPath::GetContextFromLink(name);
        name = NPath::GetTargetFromLink(name);
    }
    const auto sizeBeforeAdd = Size();
    auto targetId = TBase::Add(name);
    if (sizeBeforeAdd == targetId && NPath::IsTypedPath(name)) {
        const auto type = NPath::GetType(name);
        if (type == NPath::Source || type == NPath::Unset) {
            Meta[targetId].IsSource = true;
        }
    }
    if (!isLink) {
        return targetId;
    }
    return TFileId::CreateElemId(context, targetId);
}

bool TFileConf::IsActualExternalChangesSource(const TFileData& data) const {
    return UseExternalChangesSource && data.IsStatusUpToDate(TimeStamps.CurStamp() - 1) && !data.CantRead;
}

//TODO: we want to get rid of WasListed for directories. try to move ListDir into ReadContent
const TFileData& TFileConf::CheckFS(ui32 elemId, bool runStat, const TFileStat* fileStat) {
    TFileData& data = GetFileDataById(elemId);
    auto curStamp = TimeStamps.CurStamp();

    // we only check file system once per session
    if (data.IsStatusUpToDate(curStamp)) {
        return data;
    }

    if (DebugOptions.CompletelyTrustFSCache) {
        if (IsActualExternalChangesSource(data)) {
            // for each file from changelist/patch we call TFileConf::MarkFileAsChanged with data.LastCheckedStamp = TTimeStamps::Never
            // so for this files we not return cached data here except CantRead case which we should re-stat
            //
            // also we need emulate update(reset) 'Changed' property (like after real checking filestat)
            // or else we has unwanted read file content in TFileContentHolder::CheckForChanges
            data.Changed = data.CheckContent;
            data.LastCheckedStamp = curStamp;
            return data;
        }
    }

    TFileView nameView = GetName(elemId);

    if (!nameView.IsType(NPath::Source) && !nameView.IsType(NPath::Build)) {
        Y_ASSERT(!data.ModTime); // garbage?
        return data.MarkNotFound(curStamp);
    }

    Y_ASSERT(nameView.IsType(NPath::Source));
    Y_ASSERT(data.IsSource);

    auto targetPath = nameView.GetTargetStr();
    auto [removed, content, kind] = GetFileChanges(targetPath);
    if (removed) {
        return data.MarkNotFound(curStamp);
    }

    if (!runStat && !fileStat) {
        return data;
    }

    Stats.Inc(NStats::EFileConfStats::FileStatCount);
    TFileStat stats_;

    if (!fileStat) {
        int err = FileStat(targetPath, content, kind, stats_);
        if (!err) {
            fileStat = &stats_;
        } else if (CanTrustStatError(err)) {
            return data.MarkNotFound(curStamp);
        } else {
            NEvent::TInvalidFile event;
            event.SetFile(TString(targetPath));
            event.SetReason("Stat failed: " + TString{LastSystemErrorText(err)});
            FORCE_TRACE(P, event);

            TScopedContext context(0, targetPath, false); // We should try to re-stat such file every time
            YConfErr(BadFile) << "Cannot stat source file " << targetPath << ": " << LastSystemErrorText(err)  << " (" << err << ")"<< Endl;

            data.MarkNotFound(curStamp, true /* temporary */);
            throw TNotImplemented() << "Stat errors make graph unreliable";
            Y_UNREACHABLE();
            // return data;
        }
    }
    Y_ASSERT(fileStat && fileStat->Mode && (fileStat->MTime || UseExternalChangesSource));

    data.IsDir = fileStat->IsDir();
    if (fileStat->IsSymlink()) {
        YDIAG(Dev) << "File " << nameView << " is symlink. No follow." << Endl;
        return data.MarkNotFound(curStamp);
    } else {
        // CantRead in data means 2 things:
        // 1. CantRead && NotFound -> stat failed
        // 2. CantRead && !NotFound -> read failed
        // In both cases we treat stat uncacheable. In 2nd case here we reach with successfull `stat()`.
        // But as with other `data` fields below these are OLD values and this ensures CantRead from
        // cache will be converted to `CheckContent` before it is cleared on data update from this session.
        auto updated = IsActualExternalChangesSource(data) ? ExternalChanges.contains(elemId) : (data.ModTime != fileStat->MTime || data.Size != fileStat->Size);
        data.CheckContent |= updated || data.NotFound || data.LastCheckedStamp == TTimeStamps::Never || data.CantRead;
    }
    data.ModTime = fileStat->MTime;
    data.Size = fileStat->Size;

    return data.FinalizeStatusUpdate(true /*found*/, curStamp);
}

const TFileData& TFileConf::VCheckFS(TFileView name) {
    bool isSrc = name.IsType(NPath::Source);
    return isSrc ? CheckFS(name.GetElemId()) : GetFileDataById(name.GetElemId());
}

void TFileConf::ReadContent(ui32 id, TFileContentHolder& contentHolder) {
    TFileView view = GetName(id);
    ReadContent(view, Configuration.RealPath(view), contentHolder);
}

void TFileConf::ReadContent(TStringBuf fileName, TFileContentHolder& contentHolder) {
    auto id = Add(fileName);
    ReadContent(id, contentHolder);
}

bool TFileConf::CheckDirectory(TStringBuf name) {
    return CheckDirectory(GetStoredName(name));
}
bool TFileConf::CheckDirectory(TFileView name) {
    const auto& data = VCheckFS(name);
    return IsYPathValid(data, EPathKind::MaybeDir); // non-existent and non-usable dirs are checked in another place
}

bool TFileConf::CheckExistentDirectory(TStringBuf name) {
    return CheckExistentDirectory(GetStoredName(name));
}
bool TFileConf::CheckExistentDirectory(TFileView name) {
    const auto& data = VCheckFS(name);
    return IsYPathValid(data, EPathKind::Dir);
}

bool TFileConf::CheckLostDirectory(TFileView name) {
    const TFileData& data = VCheckFS(name);
    return (!data.IsDir || (data.IsDir && data.NotFound)) && data.Changed;  // Should be directory but not directory or not found
}

bool TFileConf::IsNonExistedSrcDir(TStringBuf name) {
    return NPath::IsType(name, NPath::Source) && !CheckExistentDirectory(name);
}

static inline TBlob SkipBOM(const TBlob& b, TStringBuf path) {
    static unsigned char bom1[] = {0xEF, 0xBB, 0xBF};
    static unsigned char bom2[] = {0xFE, 0xFF};
    static unsigned char bom3[] = {0xFF, 0xFE};
    static unsigned char bom4[] = {0, 0, 0xFE, 0xFF};
    static unsigned char bom5[] = {0xFF, 0xFE, 0, 0};

#define CHECKBOM(x)                                                          \
    if (b.Size() >= sizeof(x) && memcmp(x, b.AsCharPtr(), sizeof(x)) == 0) { \
        YDIAG(Dev) << "BOM in " << path << ", will skip it" << Endl;         \
        return b.SubBlob(sizeof(x), b.Size());                               \
    }

    CHECKBOM(bom1);
    CHECKBOM(bom2);
    CHECKBOM(bom3);
    CHECKBOM(bom4);
    CHECKBOM(bom5);

    return b;

#undef CHECKBOM
}

template <typename DataT>
inline static TMd5Sig CalcChecksum(const DataT& data) noexcept {
    TMd5Sig s;
    const uint128 hash = CityHash128WithSeed((const char*)data.data(), data.size(), uint128(0, data.size()));
    s.D0 = Uint128Low64(hash);
    s.D1 = Uint128High64(hash);
    return s;
}

void TFileConf::ReadContent(TFileView view, TString&& realPath, TFileContentHolder& contentHolder) {
    Y_ASSERT(view.GetElemId());
    TBlob blob;
    bool mapped = false;

    if (CheckFS(view.GetElemId()).NotFound) {
        NEvent::TInvalidFile event;
        event.SetFile(TString(view.GetTargetStr()));
        event.SetReason("File not found");
        TRACE(P, event);

        NEvent::TDisplayMessage msg;
        msg.SetType("Debug");
        msg.SetSub("Invalid File");
        msg.SetMod("unimp");
        msg.SetMessage(TStringBuilder() << "Check result is 'not found', filename is '" << view << "', realPath is '" << realPath << "'");
        FORCE_TRACE(U, msg);
        return;
    }

    const auto onFail = [view, &realPath](const TFileError& e) {
        NEvent::TDisplayMessage msg;
        msg.SetType("Debug");
        msg.SetSub("Invalid File");
        msg.SetMod("unimp");
        msg.SetMessage(TStringBuilder() << "FileError '" << e.what() << "' , filename is '" << view << "', realPath is '" << realPath << "'");
        FORCE_TRACE(U, msg);
        // Only retry that can be fixed with retry
        if (!EqualToOneOf(e.Status(), EAGAIN, EBUSY, EINTR, EIO, EMFILE, ENFILE, ENOMEM, ETIMEDOUT, EWOULDBLOCK)) {
            throw;
        }
    };

    TFileData& data = GetFileDataById(view.GetElemId());
    auto curStamp = TimeStamps.CurStamp();

    try {
        bool isFromExternalChanges = IsChangesContentSupported_ && ExternalChanges.contains(view.GetElemId());
        if (isFromExternalChanges) {
            Y_ABORT_UNLESS(view.IsType(NPath::Source));
            auto [content, kind] = Changes->GetFileContent(view.CutType());
            Y_ABORT_UNLESS(kind == IChanges::EFK_File);
            blob = TBlob::NoCopy(content.data(), content.size());
            Stats.Inc(NStats::EFileConfStats::FromPatchCount);
            Stats.Inc(NStats::EFileConfStats::FromPatchSize, content.size());
        } else {
            const auto read = [&]() {
                TFile f(realPath, RdOnly | Seq);

                if (data.Size < (64 << 10)) {
                    blob = TBlob::FromFileContentSingleThreaded(f);
                } else {
                    // Read 1 byte before mapping to detect access problems for encrypted and banned files in arc
                    TBlob::FromFileContentSingleThreaded(f, 0, 1);
                    mapped = true;
                    blob = TBlob::FromFile(f);
                }
            };

            const auto start = Now();
            DoWithRetry<TFileError>(read, onFail, TRetryOptions(3, TDuration::Seconds(1)), true);
            RecordStats(data.Size, start, mapped);
        }
    } catch (TFileError& e) {
        NEvent::TInvalidFile event;
        event.SetFile(TString(view.GetTargetStr()));
        event.SetReason("Read failed: " + TString{LastSystemErrorText(e.Status())});
        TRACE(P, event);

        TScopedContext context(0, view.GetTargetStr(), false); // We should try to reread such file every time
        YConfErr(BadFile) << "Cannot read source file " << view.GetTargetStr() << ": " << LastSystemErrorText(e.Status())  << " (" << e.Status() << ")"<< Endl;

        contentHolder.Content_ = TBlob();
        contentHolder.ContentReady_ = true;
        contentHolder.AbsoluteName_ = std::move(realPath);
        contentHolder.TargetId_ = view.GetElemId();

        data.MarkUnreadable(CalcChecksum(view.GetTargetStr()), curStamp);
        return;
    }

    const auto start = Now();
    contentHolder.Content_ = SkipBOM(blob, realPath);
    contentHolder.ContentReady_ = true;
    contentHolder.AbsoluteName_ = std::move(realPath);
    contentHolder.TargetId_ = view.GetElemId();

    const TMd5Sig newHashSum = CalcChecksum(blob);
    RecordMD5Stats(start, mapped);

    data.FinalizeContentUpdate(newHashSum, curStamp);
}

/// Check validity file data for kind
bool TFileConf::IsYPathValid(const TFileData& data, EPathKind kind) {
    if (EPathKind::MaybeDir == kind) {
        return data.IsDir || data.NotFound; // may be directory if found
    } else {
        return !data.NotFound && (kind == EPathKind::Any || data.IsDir == (kind == EPathKind::Dir));
    }
}

/// Check NeedFix of path and reconstruct it, if need by use pathStrBuf
TStringBuf TFileConf::YPathFix(TStringBuf path, TString& pathStrBuf) {
    if (!NPath::NeedFix(path)) {
        return path;
    }
    return pathStrBuf = NPath::Reconstruct(path);
}

/// Check exists and kind by path, if not found or invalid kind - return empty view
TFileView TFileConf::YPathExists(TStringBuf path, EPathKind kind, bool pathFixed) {
    TString pathStrBuf;
    if (pathFixed) {
        Y_ASSERT(!NPath::NeedFix(path));
    } else {
        path = YPathFix(path, pathStrBuf);
    };
    auto pathView = GetStoredName(path);
    if (YPathExists(pathView, kind)) {
        return pathView;
    } else {
        return {};
    };
}

/// Check exists and kind by pathView, if not found or invalid kind - return false
bool TFileConf::YPathExists(TFileView pathView, EPathKind kind) {
    const auto path = pathView.GetTargetStr();
    AssertEx(NPath::IsType(path, NPath::ERoot::Source), "Attempt to check non-source path " << path);
    const auto& data = VCheckFS(pathView); // This performs caching
    return IsYPathValid(data, kind);
}

/// Check file/dir exists in directory
bool TFileConf::DirHasFile(TStringBuf dirName, TStringBuf fileName, EPathKind kind) {
    AssertEx(NPath::IsType(dirName, NPath::Source), "Attempt to lookup non-source directory: " << dirName);
    return !YPathExists(NPath::Join(dirName, fileName), kind).Empty();
}

/// Check file/dir exists in directory
bool TFileConf::DirHasFile(TFileView dirView, TStringBuf fileName, EPathKind kind) {
    return DirHasFile(dirView.GetTargetStr(), fileName, kind);
}

static const TStringBuf YA_MAKE = "ya.make";

/// Check ya.make file exists in directory
bool TFileConf::DirHasYaMakeFile(TStringBuf dirName) {
    return DirHasFile(dirName, YA_MAKE, EPathKind::File);
}

/// Check ya.make file exists in directory
bool TFileConf::DirHasYaMakeFile(TFileView dirView) {
    return DirHasFile(dirView, YA_MAKE, EPathKind::File);
}

/// Return <removed, content, kind> from Changes
std::tuple<bool, TStringBuf, IChanges::EFileKind> TFileConf::GetFileChanges(TStringBuf name) const {
    if (!IsChangesContentSupported_) {
        return { false, TStringBuf(), IChanges::EFK_NotFound };
    }

    auto relPath = NPath::CutType(name);
    // Check if the file is deleted in the patch
    if (Changes->IsRemoved(relPath)) {
        return { true, TStringBuf(), IChanges::EFK_NotFound };
    }

    // Check if the file is added in the patch
    auto [content, kind] = Changes->GetFileContent(relPath);
    return { false, content, kind };
}

/// Return <found, removedOrInvalidKind> from Changes, if content supported in Changes
std::tuple<bool, bool> TFileConf::FindFileInChangesNoFS(TStringBuf name, bool allowFile, bool allowDir) const {
    if (!IsChangesContentSupported_) {
        return { false, false };
    }
    auto [removed, content, kind] = GetFileChanges(name);
    bool removedOrInvalidKind  = removed ||
        (!allowFile && (kind == IChanges::EFK_File)) ||
        (!allowDir && (kind == IChanges::EFK_Dir));
    return { IChanges::EFK_NotFound != kind, removedOrInvalidKind };
}

int TFileConf::FileStat(TStringBuf name, TStringBuf content, IChanges::EFileKind kind, TFileStat& result) const {
    Y_ASSERT(NPath::IsType(name, NPath::Source));
    if (kind != IChanges::EFK_NotFound) {
        result = {};
        if (kind == IChanges::EFK_File) {
            // regular file
            result.Mode = S_IFREG;
            result.Size = content.size();
        } else {
            Y_ASSERT(kind == IChanges::EFK_Dir);
            // directory
            result.Mode = S_IFDIR;
            result.Size = 4096;
        }
        return 0;
    }

    TString realPath = Configuration.RealPath(name);
    TCyclesTimer timer;

    ClearLastSystemError();
    result = TFileStat(realPath, true);  // nofollow = true
    int err = LastSystemError();         // run back-to-back with above to avoid error owerwite
    auto us = timer.GetUs();
    SumUsStat(us, NStats::EFileConfStats::LstatCount, NStats::EFileConfStats::LstatSumUs, NStats::EFileConfStats::LstatMinUs, NStats::EFileConfStats::LstatMaxUs);

    return result.IsNull() ? err : 0;
}

void TFileConf::ListDir(ui32 dirElemId, bool forceList, bool forceStat) {
    const TFileData& dirFData = CheckFS(dirElemId);
    auto curStamp = TimeStamps.CurStamp();

    forceList = forceList && !DirData.contains(dirElemId);
    if (!dirFData.IsDir || dirFData.NotFound || (!dirFData.CheckContent && (dirFData.IsContentUpdated(curStamp) || !forceList))) {
        return;
    }

    TFileView pathView = GetName(dirElemId);
    Base2FullNamer_.SetPath(pathView.GetTargetStr());

    TString fullPath = Configuration.RealPath(pathView);
    YDIAG(VV) << "  listing dir: " << fullPath << Endl;

    THashSet<ui32>* addedContent = nullptr;
    if (IsChangesContentSupported_ && !ChangesAddedDirContent.empty()) {
        // Add entries which were added in the patch
        if (auto iter = ChangesAddedDirContent.find(dirElemId); iter != ChangesAddedDirContent.end()) {
            addedContent = &iter->second; // edit below inplace, and clear after use
            Y_ASSERT(!addedContent->empty()); // can't be empty here, must be used one time only, and cleared after use
        }
    }

    auto& dirItems = SortedReadDir_.ReadDir(fullPath, forceStat, Stats);
    if (SortedReadDir_.IsReadFailed() && !addedContent) {
        const auto dirName = pathView.GetTargetStr();

        TRACE(P, NEvent::TInvalidSrcDir(TString{dirName}));
        YConfErr(BadDir) << "Cannot read source directory " << dirName << ": " << SortedReadDir_.ReadFailedMessage();

        GetFileDataById(dirElemId).MarkUnreadable(CalcChecksum(dirName), curStamp);
        throw TNotImplemented() << "Readdir errors make graph unreliable";
        Y_UNREACHABLE();
        // return;
    }

    const ui32 REMOVED = TFileId::RemovedElemId();// Magic value for mark removed elements in directory items list
    for (auto& dirItem: dirItems) {
        auto arcFullname  = Base2FullNamer_.GetFullname(SortedReadDir_.GetBasename(dirItem));
        if (IsChangesContentSupported_) {
            // Filter out the entries which were removed in the patch
            if (Changes->IsRemoved(NPath::CutType(arcFullname))) {
                dirItem.ElemId = REMOVED; // mark this item as removed
                continue;
            }
        }
        const auto elemId = GetStoredName(arcFullname).GetElemId();
        dirItem.ElemId = elemId;
        if (dirItem.Stat.has_value()) { // fill stat data to TFileData
            auto& fileData = CheckFS(elemId, false /* don't stat */, &dirItem.Stat.value());
            Y_ASSERT(fileData.IsStatusUpToDate(curStamp));
        } else {
            auto& fileData = GetFileDataById(elemId);
            fileData.IsDir = dirItem.IsDir;
            fileData.NotFound = false;
        }
    }

    if (addedContent) {
        // Fill IsDir flag from Changes
        auto fillAddedIsDir = [&](ui32 elemId) {
            auto addedFileView = GetName(elemId);
            auto [_, kind] = Changes->GetFileContent(addedFileView.CutType());
            // We set IsDir for all entries from Changes which are explicitly
            // marked as EFK_Dir and those entries which do not belong to Changes
            // but present in addedContent.
            auto& fileData = GetFileDataById(elemId);
            auto isDir = (kind != IChanges::EFK_File);
            fileData.IsDir = isDir;
            fileData.NotFound = false; // here all always found
            return std::tuple<TFileView, bool>(addedFileView, isDir);
        };

        for (const auto& dirItem : dirItems) {
            const auto elemId = dirItem.ElemId;
            const auto it = addedContent->find(elemId);
            if (it == addedContent->end()) {
                continue;
            }
            addedContent->erase(it); // already in dirFilenames
            fillAddedIsDir(elemId);
        }

        if (!addedContent->empty()) {
            for (auto elemId: *addedContent) {
                auto [addedFileView, isDir] = fillAddedIsDir(elemId);
                SortedReadDir_.AddItem(addedFileView.Basename(), isDir, elemId);
            }
            SortedReadDir_.ResortDirItems(); // Resort after add

            // use swap for clear and release used by THashSet memory
            THashSet<ui32> emptySet;
            std::swap(*addedContent, emptySet); // empty set is flag about was already used
        }
    }

    MD5 dirHash;
    auto& dirData = DirData[dirElemId] = {};
    for (const auto& dirItem: dirItems) {
        auto elemId = dirItem.ElemId;
        if (elemId == REMOVED) {
            continue; // item removed, skip it
        }
        dirHash.Update(SortedReadDir_.GetBasename(dirItem));
        dirData.push_back(elemId);
    }

    auto& data = GetFileDataById(dirElemId);
    TMd5Sig newHashSum;
    dirHash.Final(newHashSum.RawData);
    data.FinalizeContentUpdate(newHashSum, curStamp);
}

TFileConf::EMarkAsChangedResult TFileConf::MarkFileAsChanged(const TString& filename) {
    if (filename.size() <= NPath::PREFIX_LENGTH) {
        return EMarkAsChangedResult::DONE;
    }
    YDIAG(FU) << "Patch content: " << filename << Endl;

    auto fileExisted = HasName(filename);
    auto id = Add(filename);
    if (auto [_, inserted] = ExternalChanges.insert(id); !inserted) {
        return EMarkAsChangedResult::DONE;
    }

    if (!fileExisted && IsChangesContentSupported_) {
        auto dir_id = Add(NPath::Parent(filename));
        auto [iter, _] = ChangesAddedDirContent.try_emplace(dir_id);
        iter->second.insert(id);
    }

    auto& data = GetFileDataById(id);
    auto fileNotFound = data.NotFound;
    auto hasActualCachedData = data.IsStatusUpToDate(TimeStamps.CurStamp() - 1);

    data.LastCheckedStamp = TTimeStamps::Never;

    auto needUpdateParent = !fileExisted || fileNotFound || !hasActualCachedData;
    return needUpdateParent ? EMarkAsChangedResult::UPDATE_PARENT : EMarkAsChangedResult::DONE;
}

void TFileConf::MarkAsMakeFile(TFileView view) {
    GetFileDataById(view.GetElemId()).IsMakeFile = true;
}

void TFileConf::UseExternalChanges(THolder<IChanges> changes) {
    UseExternalChangesSource = true;

    // Changes is used inside MarkFileAsChanged
    Changes.Reset(std::move(changes));
    IsChangesContentSupported_ = Changes->IsContentSupported();

    auto&& markChanged = [this](const IChanges::TChange& change) {
        auto fileName = ArcPath(change.Name);
        auto type = change.Type;

        auto needUpdateParent = MarkFileAsChanged(fileName);
        if (type == EChangeType::Remove) {
            needUpdateParent = EMarkAsChangedResult::UPDATE_PARENT;
        }

        while (needUpdateParent == EMarkAsChangedResult::UPDATE_PARENT) {
            fileName = NPath::Parent(fileName);
            needUpdateParent = MarkFileAsChanged(fileName);
        }
    };

    Changes->Walk(markChanged);

    if (!IsChangesContentSupported_) {
        // No need to keep Changes any longer
        Changes.Reset(nullptr);
    }
}

void TFileConf::RecordStats(ui64 size, TInstant start, bool mapped) {
    const auto duration = (Now() - start).MicroSeconds();

    if (mapped) {
        Stats.Inc(NStats::EFileConfStats::MappedCount);
        Stats.Inc(NStats::EFileConfStats::MappedSize, size);
        Stats.Inc(NStats::EFileConfStats::MapTime, duration);
    } else {
        Stats.Inc(NStats::EFileConfStats::LoadedCount);
        Stats.Inc(NStats::EFileConfStats::LoadedSize, size);
        Stats.Inc(NStats::EFileConfStats::LoadTime, duration);
        Stats.SetMax(NStats::EFileConfStats::MaxLoadTime, duration);

        const int bucketSize = 10000;
        auto cnt = Stats.Get(NStats::EFileConfStats::LoadedCount);
        auto bucket = cnt / bucketSize;
        if (SubStats == nullptr) {
            SubStats = MakeHolder<NStats::TFileConfSubStats>(TString("Bucket #") + ToString(bucket));
            SubStats->Set(NStats::EFileConfSubStats::BucketId, bucket);
        }
        SubStats->Inc(NStats::EFileConfSubStats::LoadTime, duration);
        SubStats->SetMax(NStats::EFileConfSubStats::MaxLoadTime, duration);
        SubStats->Inc(NStats::EFileConfSubStats::LoadedCount);
        SubStats->Inc(NStats::EFileConfSubStats::LoadedSize, size);
        if ((cnt % bucketSize) == 0) {
            SubStats->Report();
            SubStats.Reset(nullptr);
        }
    }

    Instance()->UpdateFilesData(size, duration, Stats.Get(NStats::EFileConfStats::MappedCount) + Stats.Get(NStats::EFileConfStats::LoadedCount));
}

void TFileConf::SumUsStat(size_t us, NStats::EFileConfStats countStat, NStats::EFileConfStats sumUsStat, NStats::EFileConfStats minUsStat, NStats::EFileConfStats maxUsStat) const {
    Stats.Inc(countStat);
    Stats.Inc(sumUsStat, us);
    Stats.SetMin(minUsStat, us);
    Stats.SetMax(maxUsStat, us);
}

void TFileConf::AvrUsStat(NStats::EFileConfStats countStat, NStats::EFileConfStats sumUsStat, NStats::EFileConfStats avrUsStat) const {
    auto count = Stats.Get(countStat);
    if (count) {
        Stats.Set(avrUsStat, Stats.Get(sumUsStat) / count);
    }
}

void TFileConf::RecordMD5Stats(TInstant start, bool mapped) {
    const auto duration = (Now() - start).MicroSeconds();
    Stats.Inc(mapped ? NStats::EFileConfStats::MappedMD5Time : NStats::EFileConfStats::LoadedMD5Time, duration);
    Stats.SetMax(mapped ? NStats::EFileConfStats::MaxMappedMD5Time : NStats::EFileConfStats::MaxLoadedMD5Time, duration);
}

void TFileConf::ReportStats() const {
    AvrUsStat(NStats::EFileConfStats::LstatCount, NStats::EFileConfStats::LstatSumUs, NStats::EFileConfStats::LstatAvrUs);
    if (Stats.Get(NStats::EFileConfStats::LstatMinUs) == Max<ui64>()) {
        Stats.Set(NStats::EFileConfStats::LstatMinUs, 0);
    }
    AvrUsStat(NStats::EFileConfStats::OpendirCount, NStats::EFileConfStats::OpendirSumUs, NStats::EFileConfStats::OpendirAvrUs);
    if (Stats.Get(NStats::EFileConfStats::OpendirMinUs) == Max<ui64>()) {
        Stats.Set(NStats::EFileConfStats::OpendirMinUs, 0);
    }
    AvrUsStat(NStats::EFileConfStats::ReaddirCount, NStats::EFileConfStats::ReaddirSumUs, NStats::EFileConfStats::ReaddirAvrUs);
    if (Stats.Get(NStats::EFileConfStats::ReaddirMinUs) == Max<ui64>()) {
        Stats.Set(NStats::EFileConfStats::ReaddirMinUs, 0);
    }
    auto listdirSumUs = Stats.Get(NStats::EFileConfStats::OpendirSumUs) + Stats.Get(NStats::EFileConfStats::ReaddirSumUs);
    Stats.Set(NStats::EFileConfStats::ListDirSumUs, listdirSumUs);
    Stats.Set(NStats::EFileConfStats::LstatListDirSumUs, listdirSumUs + Stats.Get(NStats::EFileConfStats::LstatSumUs));
    Stats.Report();
    if (SubStats != nullptr) {
        SubStats->Report();
        SubStats.Reset(nullptr);
    }
}

ui32 TFileConf::CopySourceFileInto(ui32 id, TFileConf& other) const {
    TFileView name = GetName(id);
    if (name.IsType(NPath::Source)) {
        auto newId = other.Add(name.GetTargetStr());
        other.PutById(newId, GetById(id));
        Y_ASSERT(GetFileDataById(id).HashSum == other.GetFileDataById(newId).HashSum);
        return newId;
    }
    if (name.IsLink()) {
        auto newTargetId = CopySourceFileInto(ResolveLink(name).GetElemId(), other);
        if (!newTargetId) {
            return 0;
        }
        return TFileId::CreateElemId(name.GetContextType(), newTargetId);
    }
    return 0;
}

void TFileConf::CopySourceFilesInto(TFileConf& other) const {
    for (ui32 id = 1; id < Size(); id++) {
        CopySourceFileInto(id, other);
    }
}

TFileView TFileConf::GetStoredName(TStringBuf name) {
    ui32 elemId = Add(name);
    return GetName(elemId);
}

TFileView TFileConf::GetName(ui32 elemId) const {
    auto fileId = TFileId::Create(elemId);
    auto targetId = fileId.GetTargetId();
    TFileView target = TBase::GetName(targetId);
    if (!fileId.IsLink()) {
        return target;
    }
    return target.IsValid() ? TFileView{&NameStore, fileId.GetElemId()} : TFileView{};
}

TFileView TFileConf::GetTargetName(ui32 elemId) const {
    return TBase::GetName(TFileId::Create(elemId).GetTargetId());
}

bool TFileConf::HasName(TStringBuf name) const {
    bool isLink = NPath::IsLink(name);
    return isLink ? TBase::HasName(NPath::GetTargetFromLink(name)) : TBase::HasName(name);
}

ui32 TFileConf::GetId(TStringBuf name) const {
    bool isLink = NPath::IsLink(name);
    if (isLink) {
        TStringBuf context = NPath::GetContextFromLink(name);
        TStringBuf target = NPath::GetTargetFromLink(name);
        ui32 targetId = TBase::GetId(target);
        return targetId ? TFileId::CreateElemId(context, targetId) : 0;
    }
    return TBase::GetId(name);
}

ui32 TFileConf::GetIdNx(TStringBuf name) const {
    bool isLink = NPath::IsLink(name);
    if (isLink) {
        TStringBuf context = NPath::GetContextFromLink(name);
        TStringBuf target = NPath::GetTargetFromLink(name);
        ui32 targetId = TBase::GetIdNx(target);
        return targetId ? TFileId::CreateElemId(context, targetId) : 0;
    }
    return TBase::GetIdNx(name);
}

const TFileData& TFileConf::GetFileData(TFileView name) const {
    return GetFileDataById(name.GetElemId());
}

TFileData& TFileConf::GetFileData(TFileView name) {
    return GetFileDataById(name.GetElemId());
}

const TFileData& TFileConf::GetFileDataById(ui32 elemId) const {
    return Meta[TFileId::Create(elemId).GetTargetId()];
}

TFileData& TFileConf::GetFileDataById(ui32 elemId) {
    return Meta[TFileId::Create(elemId).GetTargetId()];
}


const TFileData& TFileConf::GetFileDataByIdWithStatusUpdate(ui32 elemId, bool stat) {
    const auto& data = GetFileDataById(elemId);
    if (IsStatusUpToDate(data)) {
        return data;
    } else {
        return CheckFS(elemId, stat);
    }
}

bool TFileConf::IsStatusUpToDate(const TFileData& data) const {
    return data.IsStatusUpToDate(TimeStamps.CurStamp());
}

bool TFileConf::IsContentUpdated(const TFileData& data) const {
    return data.IsContentUpdated(TimeStamps.CurStamp());
}

THolder<TFileContentHolder> TFileConf::GetFileById(ui32 elemId) {
    auto fileId = TFileId::Create(elemId);
    auto targetId = fileId.GetTargetId();
    if (fileId.IsLink()) {
        auto result = GetFileById(targetId);
        result->OriginalId = elemId;
        return result;
    }
    return THolder<TFileContentHolder>(new TFileContentHolder(*this, targetId, {}));
}

THolder<TFileContentHolder> TFileConf::GetFileByName(TStringBuf name) {
    return GetFileByName(GetStoredName(name));
}

THolder<TFileContentHolder> TFileConf::GetFileByName(TFileView name) {
    return GetFileById(name.GetElemId());
}

THolder<TFileContentHolder> TFileConf::GetFileByAbsPath(TStringBuf path) {
    return THolder<TFileContentHolder>(new TFileContentHolder(*this, 0, TString(path)));
}

bool TFileConf::IsPrefixOf(TFileView view, TStringBuf path) const {
    TString str;
    view.GetStr(str);
    return NPath::IsPrefixOf(str, path);
}

TFileView TFileConf::SrcDir() const {
    return SourceDir;
}

TFileView TFileConf::BldDir() const {
    return BuildDir;
}

TFileView TFileConf::DummyFile() const {
    return BuildDummyFile;
}

TFileView TFileConf::ResolveLink(TFileView view) const {
    if (view.IsLink()) {
        return GetName(view.GetTargetId());
    }
    return view;
}

TFileView TFileConf::ResolveLink(ui32 elemId) const {
    return ResolveLink(GetName(elemId));
}

TFileView TFileConf::Parent(TFileView view) {
    Y_ASSERT(!view.IsLink());
    TStringBuf str = view.GetTargetStr();
    TStringBuf parentStr = NPath::Parent(str);
    if (parentStr.empty()) {
        return {};
    }
    return GetStoredName(parentStr);
}

TFileView TFileConf::ReplaceRoot(TFileView view, NPath::ERoot root) {
    return GetStoredName(NPath::ConstructPath(view.CutType(), root));
}

TFileView TFileConf::ConstructLink(ELinkType context, TFileView target) {
    if (!target.IsValid()) {
        return {};
    }
    return TFileView{ target.Table, TFileId::CreateElemId(context, target.GetTargetId()) };
}

TString TFileConf::ConstructLink(ELinkType context, TStringBuf target) {
    NPath::Validate(target);
    return ELinkTypeHelper::LinkToTargetString(context, target);
}

TString TFileConf::ConstructPathWithLink(ELinkType context, TStringBuf target, NPath::ERoot root) {
    Y_ASSERT(!NPath::IsLink(target));
    TString path = NPath::ConstructPath(target, root);
    return ELinkTypeHelper::LinkToTargetString(context, path);
}

TFileView TFileConf::CreateFile(TFileView dir, TStringBuf filename) {
    TStringBuf str = dir.GetTargetStr();
    TString fileStr = NPath::Join(str, filename);
    return GetStoredName(fileStr);
}

bool TFileConf::IsLink(ui32 elemId) {
    return TFileId::Create(elemId).IsLink();
}

ui32 TFileConf::GetTargetId(ui32 elemId) {
    return TFileId::Create(elemId).GetTargetId();
}

TStringBuf TFileConf::GetContextStr(ui32 elemId) {
    auto fileId = TFileId::Create(elemId);
    Y_ENSURE(fileId.IsLink());
    return fileId.GetLinkName();
}

ELinkType TFileConf::GetContextType(TStringBuf context) {
    return ELinkTypeHelper::Name2Type(context);
}

TFileContentHolder::TFileContentHolder(TFileConf& fileConf, ui32 targetId, TString&& absName)
    : FileConf_(fileConf)
    , TargetId_(targetId)
    , IsSource_(TargetId_ ? FileConf_.GetFileDataById(TargetId_).IsSource : false)
    , AbsoluteName_(std::move(absName))
{}

bool TFileContentHolder::CheckForChanges(ECheckForChangesMethod checkMethod) {
    const auto& data = GetFileData();
    if (WasRead() || checkMethod == ECheckForChangesMethod::RELAXED || data.NotFound || FileConf_.IsContentUpdated(data)) {
        return data.Changed;
    }
    if (!data.Changed) {
        return false;
    }
    if (!data.IsDir) {
        FileConf_.ReadContent(TargetId_, *this);
    } else {
        FileConf_.ListDir(TargetId_);
    }
    // We can't use &data here, it may be invalidate in ListDir
    const auto& updatedData = GetFileData();
    return updatedData.Changed;
}

const TFileData& TFileContentHolder::GetFileData() const {
    if (IsSource_) {
        return FileConf_.CheckFS(TargetId_);
    } else {
        return FileConf_.GetFileDataById(TargetId_);
    }
}

TFileData& TFileContentHolder::GetFileData() {
    if (IsSource_) {
        FileConf_.CheckFS(TargetId_); // return const&, continue and get non-const link below
    }
    return FileConf_.GetFileDataById(TargetId_);
}

TFileView TFileContentHolder::GetName() const {
    if (TargetId_) {
        return FileConf_.GetName(TargetId_);
    }
    return {};
}

ELinkType TFileContentHolder::GetProcessingContext() const {
    if (OriginalId) {
        return FileConf_.GetName(OriginalId).GetContextType();
    }
    return {};
}

TStringBuf TFileContentHolder::GetAbsoluteName() {
    if (AbsoluteName_.empty()) {
        TFileView path = GetName();
        Y_ASSERT(path.GetTargetStr().back() != NPath::PATH_SEP);
        AbsoluteName_ = FileConf_.Configuration.RealPath(path);
    }
    return AbsoluteName_;
}

ui32 TFileContentHolder::GetTargetId() const {
    return TargetId_;
}

bool TFileContentHolder::IsInternalLink() const {
    return OriginalId != 0;
}

void TFileContentHolder::ReadContent() {
    if (!ContentReady_) {
        YDIAG(VV) << "Read content of " << AbsoluteName_ << Endl;
        ContentReady_ = true;
        if (TargetId_) {
            FileConf_.ReadContent(TargetId_, *this);
        } else {
            Y_ASSERT(AbsoluteName_);
            FileConf_.ReadContent(AbsoluteName_, *this);
        }
    }
}

TStringBuf TFileContentHolder::GetContent() {
    ReadContent();
    return {Content_.AsCharPtr(), Content_.Size()};
}

void TFileContentHolder::UpdateContentHash() {
    if (CheckForChanges(ECheckForChangesMethod::RELAXED)) {
        ReadContent();
    }
}

bool TFileContentHolder::WasRead() const {
    return ContentReady_;
}

size_t TFileContentHolder::Size() const {
    return ContentReady_ ? Content_.Size() : 0;
}

void TFileContentHolder::ValidateUtf8(const TStringBuf fileName) {
    if (WasRead() && Size()) {
        if (!IsUtf(GetContent())) {
            throw yexception() << "File '" << fileName << "' has non-UTF8 symbols";
        }
    }
}

void TFileView::GetStr(TString& name) const {
    if (!Table) {
        name = {};
    } else {
        auto targetBuf = GetTargetStr();
        if (IsLink()) {
            name = NPath::ConstructPath(NPath::Join(GetLinkName(), targetBuf), NPath::Link);
        } else {
            name = targetBuf;
        }
    }
}

TStringBuf TFileView::GetTargetStr() const {
    return Table
        ? Table->GetStringBufName(GetTargetId())
        : TStringBuf{};
}

bool TFileView::IsValid() const {
    return Table
        ? Table->CheckId(GetTargetId())
        : false;
}

bool TFileView::operator==(const TFileView& view) const {
    return (Table == view.Table) && (GetElemId() == view.GetElemId());
}

std::strong_ordering TFileView::operator<=>(const TFileView& view) const {
    if ((Table == view.Table) && (GetElemId() == view.GetElemId())) {
        return std::strong_ordering::equal;
    }
    auto linkType = GetLinkType();
    auto otherLinkType = view.GetLinkType();
    if (linkType != otherLinkType) {
        return linkType <=> otherLinkType;
    }
    return GetTargetStr().compare(view.GetTargetStr()) <=> 0;
}

bool operator==(TStringBuf str, TFileView view) {
    if (view.IsLink()) {
        TString viewStr;
        view.GetStr(viewStr);
        return viewStr == str;
    }
    return view.GetTargetStr() == str;
}

bool operator==(TFileView view, TStringBuf str) {
    return str == view;
}

IOutputStream& operator<<(IOutputStream& out, TFileView view) {
    if (view.IsLink()) {
        out << "$L" << NPath::PATH_SEP << view.GetContextFromLink() << NPath::PATH_SEP;
    }
    out << view.GetTargetStr();
    return out;
}

bool TFileView::IsLink() const {
    Y_ASSERT(!Empty());
    return TFileId::IsLink();
}

bool TFileView::IsType(NPath::ERoot root) const {
    return IsLink()
        ? root == NPath::Link
        : NPath::IsType(GetTargetStr(), root);
}

bool TFileView::InSrcDir() const {
    return IsType(NPath::Source);
}

NPath::ERoot TFileView::GetType() const {
    return IsLink()
        ? NPath::Link
        : NPath::GetType(GetTargetStr());
}

TStringBuf TFileView::CutAllTypes() const {
    Y_ASSERT(!IsLink());
    return NPath::CutAllTypes(GetTargetStr());
}

TStringBuf TFileView::CutType() const {
    Y_ASSERT(!IsLink());
    return NPath::CutType(GetTargetStr());
}

TStringBuf TFileView::Basename() const {
    return NPath::Basename(GetTargetStr());
}

TStringBuf TFileView::BasenameWithoutExtension() const {
    return NPath::BasenameWithoutExtension(GetTargetStr());
}

TStringBuf TFileView::Extension() const {
    return NPath::Extension(GetTargetStr());
}

TStringBuf TFileView::NoExtension() const {
    Y_ASSERT(!IsLink());
    return NPath::NoExtension(GetTargetStr());
}

TStringBuf TFileView::GetContextFromLink() const {
    Y_ASSERT(IsLink());
    return GetLinkName();
}

ELinkType TFileView::GetContextType() const {
    return GetLinkType();
}

void TFileView::UpdateMD5(MD5& md5) const {
    md5.Update(GetTargetStr());
}
