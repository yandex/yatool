#include "roots_options.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/symbols/symbols.h>

#include <util/folder/pathsplit.h>

using NPath::TUnixPath;

bool TRootsOptions::NeedNormalizeRealPath() const {
    // On non-Windows systems this should be false at compile time
    return TPathSplitTraitsLocal::IsPathSep('\\') && NormalizeRealPath;
}

void TRootsOptions::AddOptions(NLastGetopt::TOpts& opts) {
    opts.AddLongOption('S', "source-root").StoreResult(&SourceRoot);
    opts.AddLongOption('B', "build-root").StoreResult(&BuildRoot).Required();
    opts.AddLongOption('b', "tests-data-root").StoreResult(&ArcadiaTestsDataRoot);
}

void TRootsOptions::PostProcess(const TVector<TString>& /* freeArgs */) {
    if (BuildRoot.Exists() && !BuildRoot.IsDirectory()) {
        throw TConfigurationError() << "build root is not a directory";
    }

    BuildRoot.MkDirs();
}

// Note: returns path either in host or in Unix format depending on NeedNormalizeRealPath()
TString TRootsOptions::RealPathByStr(TStringBuf p) const {
    TStringBuf file = NPath::ResolveLink(p);
    if (NPath::IsExternalPath(file)) {
        return TString{file};
    }

    const TFsPath& root = RealPathRoot(file);
    if (!root.IsDefined()) {
        return TString{};
    }
    if (file.starts_with(root.GetPath())) {
        return TString{file};
    }

    TStringBuf loc = NPath::CutType(file);
    if (!loc) {
        return root.c_str();
    }

    TString path;
    if (NeedNormalizeRealPath()) {
        path = TUnixPath(TPathSplit(root.PathSplit()).ParsePart(loc)).Reconstruct();
    } else {
        if (!NPath::NeedFix(loc)) {
            return NPath::Join(root.c_str(), loc);
        }
        path = TPathSplit(root.PathSplit()).ParsePart(loc).Reconstruct();
    }
    if (path.StartsWith("..")) {
        TRACE(U, NEvent::TFileOutsideRoots(path));
        TString outsidePath = root.c_str();
        if (NeedNormalizeRealPath()) {
            outsidePath.push_back(TPathSplitTraitsUnix::MainPathSep);
            outsidePath += TUnixPath(loc).Reconstruct();
        } else {
            outsidePath.push_back(TPathSplitTraitsLocal::MainPathSep);
            outsidePath += TPathSplit(loc).Reconstruct();
        }
        return outsidePath;
    }
    return path;
}

// Note: returns path either in host or in Unix format depending on NeedNormalizeRealPath()
TString TRootsOptions::RealPath(const TStringBuf& p1, const TStringBuf& p2) const {
    if (NeedNormalizeRealPath()) {
        return TUnixPath(TPathSplit(RealPathRoot(p1).PathSplit()).ParsePart(NPath::CutType(p1)).ParsePart(p2)).Reconstruct();
    } else {
        return TPathSplit(RealPathRoot(p1).PathSplit()).ParsePart(NPath::CutType(p1)).ParsePart(p2).Reconstruct();
    }
}

// Note: returns path either in host or in Unix format depending on NeedNormalizeRealPath()
TString TRootsOptions::RealPath(const TStringBuf& p1, const TStringBuf& p2, const TStringBuf& p3) const {
    if (NeedNormalizeRealPath()) {
        return TUnixPath(TPathSplit(RealPathRoot(p1).PathSplit()).ParsePart(NPath::CutType(p1)).ParsePart(p2).ParsePart(p3)).Reconstruct();
    } else {
        return TPathSplit(RealPathRoot(p1).PathSplit()).ParsePart(NPath::CutType(p1)).ParsePart(p2).ParsePart(p3).Reconstruct();
    }
}

const TFsPath& TRootsOptions::RealPathRoot(const TStringBuf& p) const {
    if (NPath::IsType(p, NPath::Unset)) {
        YErr() << "File location still unknown: " << p << Endl;
        return Default<TFsPath>();
    }

    if (NPath::IsExternalPath(p)) {
        return Default<TFsPath>();
    }

    Y_ASSERT(NPath::IsType(p, NPath::Source) || NPath::IsType(p, NPath::Build));
    Y_ASSERT(p.at(0) != NPath::PATH_SEP);

    return NPath::IsType(p, NPath::Source) ? SourceRoot : BuildRoot;
}

struct TFakeView {
    TStringBuf Path;
    std::optional<ui64> ElemId;
    bool HasId() const {
        return ElemId.has_value();
    }
    ui32 GetTargetId() const {
        return ElemId ? *ElemId : 0;
    }
    TStringBuf GetTargetStr() const {
        return NPath::ResolveLink(Path);
    }
    bool IsLink() const {
        return NPath::IsLink(Path);
    }
};

// Note: returns path in host platform format
TString TRootsOptions::RealPath(TStringBuf path, std::optional<ui64> elemId) const {
    TFakeView view{path, elemId};
    return RealPath(view);
}

// Note: returns path in host platform format
TString TRootsOptions::RealPathEx(TStringBuf path, std::optional<ui64> elemId) const {
    TString res = RealPath(path, elemId);
    Y_ASSERT(!res.empty());
    return res;
}

void TRootsOptions::EnableRealPathCache(TFileConf* refNames) {
    auto& cache = PathsCache.Get();
    cache.clear();
    RefNames = refNames;
    if (RefNames != nullptr) {
        cache.resize(RefNames->Size());
    }
}

bool TRootsOptions::CanonPath(const TStringBuf& abspath, TString& result) const {
    TFsPath abs(abspath);
    if (abs.IsSubpathOf(BuildRoot)) {
        result = NPath::NormalizeSlashes(NPath::ConstructPath(abs.RelativeTo(BuildRoot).c_str(), NPath::Build));
        return true;
    }
    if (abs.IsSubpathOf(SourceRoot)) {
        result = NPath::NormalizeSlashes(NPath::ConstructPath(abs.RelativePath(SourceRoot).c_str(), NPath::Source));
        return true;
    }
    return false;
}

TString TRootsOptions::CanonPath(const TStringBuf& abspath) const {
    TString result;
    if (CanonPath(abspath, result)) {
        return result;
    } else {
        TRACE(U, NEvent::TFileOutsideRoots(abspath.data()));
        return abspath.data();
    }
}
