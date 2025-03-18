#include "path_resolver.h"

#include <devtools/ymake/symbols/time_store.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/manager.h>

namespace {
    bool CutRoot(TStringBuf& path, TStringBuf what) {
        if (path.StartsWith(what)) {
            path.Skip(what.length());
            if (path.front() == NPath::PATH_SEP) {
                path.Skip(1);
            }
            return true;
        }
        return false;
    }

    TString PrepareName(TStringBuf name) {
        name = NPath::ResolveLink(name);
        TStringBuf result = name;
        if (NPath::IsTypedPathEx(name) && NPath::IsType(name, NPath::Unset)) {
            result = NPath::CutType(name);
        }
        CutRoot(result, TStringBuf("./"));
        if (NPath::HasWinSlashes(result)) {
            return NPath::NormalizeSlashes(result);
        } else {
            return TString{result};
        }
    }

    TStringBuf FindCommonParent(const TStringBuf dir, const TStringBuf path, TStringBuf& afterParent) {
        auto dirSize = dir.size();
        if (!dirSize) { // if dir empty, afterParent make no sense
            return {};
        }
        const auto* pDir = dir.data();
        const auto* pDirEnd = pDir + dirSize;
        const auto* pPathBeg = path.data();
        const auto* pPath = pPathBeg;
        const auto* pPathEnd = pPath + path.size();
        const auto* pSlash = pPathBeg; // last slash of common dir in path

        while (pPath < pPathEnd) {
            auto charOfDir = *pDir;
            auto charOfPath = *pPath;
            if (charOfDir != charOfPath) break;
            if (charOfPath == NPath::PATH_SEP) {
                pSlash = pPath;
            }
            ++pPath;
            if (++pDir >= pDirEnd) { // dir completed
                // if next char of path is slash - update last slash pointer
                if ((pPath < pPathEnd) && (*pPath == NPath::PATH_SEP)) {
                    pSlash = pPath;
                }
                break;
            }
        }

        if (pSlash == pPathBeg) {// if no common parent, afterParent make no sense
            return {};
        }

        const auto* pNextSlash = pSlash + 1;
        while (pNextSlash < pPathEnd) {
            if (*pNextSlash == NPath::PATH_SEP) { // found next slash
                break;
            }
            ++pNextSlash;
        }
        afterParent = TStringBuf{pPathBeg, pNextSlash}; // path from begin to next slash
        return {pPathBeg, pSlash}; // path from begin to last slash is common parent
    }
}

template<>
void Out<std::reference_wrapper<const TResolveVariant>>(IOutputStream& os, TTypeTraits<std::reference_wrapper<const TResolveVariant>>::TFuncParam v) {
    const auto& variant = v.get();
    variant.Path.Out(os, variant.FileConf);
    os << "@" << variant.Dir;
}

TPathResolver::TPathResolver(TResolveContext& context, bool forcedListDir)
    : Context_(context)
    , ForcedListDir_(forcedListDir)
    , FileConf_(context.Graph.Names().FileConf)
{}

EResolveStatus TPathResolver::ResolveAsKnown(TFileView curDir, bool check) {
    bool successPath = true;
    if (!NPath::IsTypedPathEx(Name_)) {
        do {
            // ToYPath fill output by construct new TString, usage Name as input and output is safe
            if (NPath::ToYPath(Name_, Name_, curDir.GetTargetStr())) { // ${ARCADIA_ROOT}, ${ARCADIA_BUILD_ROOT}, ${CURDIR}, ${BINDIR}
                break;
            }
            // CanonPath use TFsPath inside, where copy Name, usage Name as input and output is safely
            if ((Options_.AllowAbsRoot) && (Context_.Conf.CanonPath(Name_, Name_))) {
                break;
            }
            successPath = false;
        } while (0);
    }
    if (check) {
        ClearResults();
    }
    if (successPath) {
        if (NeedFix_) {
            Name_ = NPath::Reconstruct(Name_);
        }
        if (!check) {
            return RESOLVE_SUCCESS;
        }
        Result_ = CheckByRoot(TFileView());
        return IsResolved() ? RESOLVE_SUCCESS : RESOLVE_FAILURE_MISSING;
    } else {
        if ((NeedFix_) && (NPath::IsTypedPathEx(Name_))) {
            Name_ = NPath::Reconstruct(Name_);
        }
        return RESOLVE_FAILURE;
    }
}

/// @brief Set name for resolving
/// This also clears previous resolving results
void TPathResolver::SetName(TStringBuf name) {
    Name_ = PrepareName(name);
    if (NPath::IsLink(name)) {
        NameContext_ = TFileConf::GetContextType(NPath::GetContextFromLink(name));
    } else {
        NameContext_ = ELinkType::ELT_Default;
    }
    NeedFix_ = NPath::NeedFix(Name_);
    ClearResults();
}

TResolveFile TPathResolver::CheckSourceCacheParent(TFileView dir, TStringBuf path, bool wasReconstructed) {
    Y_ASSERT(dir.Empty() || NPath::IsType(dir.GetTargetStr(), NPath::Source));

    // This tries to obtain file status without going to FS
    auto [doneFast, fileFast] = CheckSourceById(path);
    if (doneFast) {
        return fileFast; // this may be empty if file absence is proven by changelist
    }

    auto [foundInChanges, removedOrInvalidKind] = FileConf_.FindFileInChangesNoFS(path, true, Options_.AllowDir);
    if (removedOrInvalidKind) {
        return {}; // not found
    }
    if (foundInChanges) {
        return TResolveFile(FileConf_.GetStoredName(path));
    }

    if (Options_.NoFS) { // recache source forbidden, check of symbol table already executed in CheckSourceById
        return {}; // not found
    }

    auto [doneFirstParent, fileFirstParent] = CheckFirstParent(dir, path, wasReconstructed);
    if (doneFirstParent) {
        return fileFirstParent; // this may be empty if file absence is proven by first parent check
    }

    TStringBuf directParent = NPath::Parent(path); // direct parent of path
    auto [_, directParentRemovedOrInvalidKind] = FileConf_.FindFileInChangesNoFS(directParent, false, true);
    if (directParentRemovedOrInvalidKind) {
        return {}; // not found
    }

    ListDir(directParent);

    auto [done, file] = CheckSourceById(path, true); // retry check with do data up to date, if need
    return file; // if incomplete empty result returned
}

TResolveFile TPathResolver::CheckBuilt(TStringBuf path) {
    Y_ASSERT(NPath::IsType(path, NPath::Build));

    const auto fileId = FileConf_.GetIdNx(path);
    if (!fileId) {
        // File name is not known at all
        return {};
    }

    if (Context_.OwnEntries.has(fileId)) {
        // File known to be owned by module (from ya.make parsing)
        return TResolveFile(FileConf_.GetName(fileId));
    }

    // File is not owned by module: lookup graph
    const auto nodeId = Context_.Graph.GetFileNodeById(fileId).Id();
    if (nodeId == TNodeId::Invalid) {
        return {};
    }

    // File was restored from cache but wasn't flushed this time: check if it was processed
    auto status = Context_.CheckNodeStatus({EMNT_NonParsedFile, fileId});

    if (status == NGraphUpdater::ENodeStatus::Ready) {
        // File was already processed and flushed into graph
        return TResolveFile(FileConf_.GetName(fileId));
    }

    if (status != NGraphUpdater::ENodeStatus::Unknown && Context_.Graph.GetFileNodeData(fileId).NodeModStamp == FileConf_.TimeStamps.CurStamp()) {
        // File was already processed and flushed into graph, but GraphUpdater is going to revisit the node.
        // We can safely resolve into this file: at this stage it can not be removed.
        return TResolveFile(FileConf_.GetName(fileId));
    }

    // To imitate first time behaviour we forbid resolving into this file before its node was processed by GraphUpdater
    return {};
}

/// Locate path in symbol table and check its presence and whether it is directory
/// Return <resolve completed, fileview for success resolve> for path
std::tuple<bool, TResolveFile> TPathResolver::CheckSourceById(TStringBuf path, bool makeUpToDate) const {
    Y_ASSERT(NPath::IsType(path, NPath::Source));

    auto elemId = FileConf_.GetIdNx(path);
    if (!elemId) {
        return { false, {} }; // incomplete, not found neither in cache nor in changes, but may stiil be on FS
    }

    const auto& data = FileConf_.GetFileDataByIdWithStatusUpdate(elemId, false /* don't stat */);
    bool uptoDate = FileConf_.IsStatusUpToDate(data);
    if (!uptoDate && !makeUpToDate) {
        return { false, {} }; // incomplete - old data, need refresh
    }

    const auto& uptodateData = uptoDate ? data : FileConf_.GetFileDataByIdWithStatusUpdate(elemId, true /* now let's stat */);
    if (!FileConf_.IsYPathValid(uptodateData, Options_.AllowDir ? EPathKind::Any : EPathKind::File)) {
        return { true, {} }; // complete - not found or invalid kind
    }
    return { true, TResolveFile(FileConf_.GetName(elemId)) }; // complete - found and valid kind
}

/// Call TFileConf::ListDir inside resolve specific settings
void TPathResolver::ListDir(TStringBuf dir) const {
    auto dirElemId = FileConf_.GetStoredName(dir).GetElemId();
    const auto& dirData = FileConf_.GetFileDataById(dirElemId);
    if (dirData.CheckContent || !FileConf_.IsStatusUpToDate(dirData)) {
        FileConf_.ListDir(dirElemId, ForcedListDir_, true); // recache direct parent and lstat() all files in it
    }
}

bool TPathResolver::IsDir() const {
    if ((Options_.AllowDir) && (!Result_.Empty())) {
        const auto targetId = Result_.GetTargetId();
        Y_ASSERT(targetId);
        const TFileData& data = FileConf_.GetFileDataById(targetId);
        return data.IsDir;
    } else {
        return false;
    }
}

bool TPathResolver::IsOutOfDir() const {
    Y_ASSERT(IsResolved());
    return NeedFix_ && ResultDir_.IsValid() && !Result_.GetTargetBuf(FileConf_).StartsWith(ResultDir_.GetTargetStr());
}

std::tuple<bool, TResolveFile> TPathResolver::CheckFirstParent(const TFileView dir, const TStringBuf path, bool wasReconstructed) {
    if (dir.Empty()) {
        return { false, {} }; // has no dir (and commonParent too): check futher
    }
    TStringBuf commonParent = dir.GetTargetStr();
    TStringBuf afterCommonParent; // next directory after commonParent or file name, if path is file in commonParent
    if (wasReconstructed) {
        commonParent = FindCommonParent(commonParent, path, afterCommonParent);
    } else {
        commonParent = { path.begin(), commonParent.size() }; // use path string for commonParent instead FileStore
        auto pos = path.find(NPath::PATH_SEP, commonParent.size() + 1);
        afterCommonParent = { path.begin(), pos != path.npos ? pos : path.size() };
    }
    ListDir(commonParent);
    auto elemId = FileConf_.GetIdNx(afterCommonParent); // and search afterParent in it
    if (!elemId) {
        // Expected path part is not in the directory: done, check failed
        return { true, {} };
    }
    if (afterCommonParent.size() == path.size()) { // path is immediate child of commonParent
        const auto& data = FileConf_.GetFileDataByIdWithStatusUpdate(elemId, true);
        if (!FileConf_.IsYPathValid(data, Options_.AllowDir ? EPathKind::Any : EPathKind::File)) {
            return {true, {}}; // not found after recache commonParent or invalid type - done, check failed
        }
        return { true, TResolveFile(FileConf_.GetName(elemId)) }; // path exists and valid, return pathView
    }
    return { false, {} }; // Expected part is in the directory: check further
}

TResolveFile TPathResolver::CheckByRoot(TFileView dir) {
    AssertEx(!Name_.empty(), "Name is not set for resolving");
    TStringBuf pathBuf;
    if (!dir.Empty()) { // validity of dir already checked before call CheckByRoot, we can check only empty
        if (Name_[0] == NPath::PATH_SEP) {
            TempStr_ = NPath::Join(dir.GetTargetStr(), Name_.substr(1));
        } else {
            TempStr_ = NPath::Join(dir.GetTargetStr(), Name_);
        }
        pathBuf = TempStr_;
    } else {
        pathBuf = Name_;
    }

    auto pathBufSizeBeforeFix = pathBuf.size();
    if (NeedFix_) {
        pathBuf = TempStr_ = NPath::Reconstruct(pathBuf);
        if (NPath::IsTypedOutOfTree(pathBuf)) { // after fix path out of tree, for example, $S/../file
            return {}; // not found
        }
    }

    switch (NPath::GetType(pathBuf)) {
        case NPath::Source:
            return CheckSourceCacheParent(dir, pathBuf, pathBufSizeBeforeFix != pathBuf.size());
        case NPath::Build:
            return CheckBuilt(pathBuf);
        default:
            throw TError() << "Resolver::CheckByRoot: Invalid root '" << pathBuf << "'";
    }
}

bool TPathResolver::CheckDirectory(TFileView dir) {
    if (!dir.IsValid()) {
        return false;
    }
    Y_ASSERT(!Name_.empty());
    Y_ASSERT(!dir.GetTargetStr().EndsWith(NPath::PATH_SEP));
    TResolveFile pathView = CheckByRoot(dir);
    if (!pathView.Empty()) {
        if (Result_.Empty()) {
            Result_ = pathView;
            ResultDir_ = dir;
        }
        if (Options_.AllVariants) {
            AddTo(TResolveVariant{.Path = TResolveFile(pathView), .Dir = dir, .FileConf = FileConf_}, Variants_);
            return false; // Proceed further
        }
        return true;
    }
    return false;
}

/// May be resolved from TFileView
TResolveFile::TResolveFile(TFileView fileView)
    : TFileId(fileView.GetElemId())
    , Root_(fileView.Empty()
        ? NPath::ERoot::Unset
        : NPath::GetType(fileView.GetTargetStr())
    )
{}

/// Construct TFileId by name, linkType and resolved flag
static TFileId MakeFileId(TFileConf& fileConf, TStringBuf name, ELinkType linkType, bool resolved) {
    Y_ASSERT(!name.empty());
    TFileView fileView;
    if (resolved) {
        if (linkType == ELT_Default) {
            fileView = fileConf.GetStoredName(name);
        } else {
            fileView = fileConf.GetStoredName(TFileConf::ConstructLink(linkType, name));
        }
    } else {
        if (linkType != ELT_Default) {
            fileView = fileConf.GetStoredName(TFileConf::ConstructPathWithLink(linkType, name, NPath::Unset));
        } else if (NPath::IsTypedPathEx(name) && (NPath::ERoot::Unset == NPath::GetType(name))) {
            fileView = fileConf.GetStoredName(name);
        } else {
            fileView = fileConf.GetStoredName(NPath::ConstructPath(name, NPath::Unset));
        }
    }
    return TFileId::Create(fileView.GetElemId());
}

/// Make resolved / unresolved from TStringBuf
TResolveFile::TResolveFile(TFileConf& fileConf, TStringBuf name, ELinkType linkType, bool resolved)
    : TFileId(MakeFileId(fileConf, name, linkType, resolved))
    , Root_(resolved ? NPath::GetType(name) : NPath::ERoot::Unset)
{}
