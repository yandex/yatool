#pragma once

#include <devtools/ymake/symbols/name_store.h>

#include <library/cpp/getopt/small/last_getopt.h>

#include <util/folder/path.h>
#include <util/system/tls.h>

class TFileConf;

struct TRootsOptions {
    void AddOptions(NLastGetopt::TOpts& opts);
    void PostProcess(const TVector<TString>& freeArgs);

    // translate internal rooted path into absolute filesystem pathname
    TString RealPath(const TStringBuf& p1, const TStringBuf& p2) const;
    TString RealPath(const TStringBuf& p1, const TStringBuf& p2, const TStringBuf& p3) const;

    // Cache-accelerated versions work for elements of symbol table (RefNames)
    TString RealPath(TStringBuf path, std::optional<ui64> elemId) const;
    TString RealPathEx(TStringBuf path, std::optional<ui64> elemId) const;

    template<typename TView>
    TString RealPath(TView view) const {
        if (!IsRealPathCacheEnabled() || !view.HasId()) {
            return RealPathByStr(view.GetTargetStr());
        }
        ui32 targetId = view.GetTargetId();
        if (targetId == 0) {
            return TString();
        }

        auto& cache = PathsCache.Get();
        if (targetId >= cache.size()) {
            cache.resize(targetId + 1);
        }

        if (Y_UNLIKELY(cache[targetId].empty())) {
            TString res = RealPathByStr(view.GetTargetStr());
            cache[targetId] = res;
            return res;
        }
        return cache[targetId];
    }

    template<typename TView>
    TString RealPathEx(TView view) const {
        TString res = RealPath(view);
        Y_ASSERT(!res.empty());
        return res;
    }

    template<>
    TString RealPath(TStringBuf view) const {
        return RealPath(view, std::nullopt);
    }

    template<>
    TString RealPath(TString view) const {
        return RealPath(view, std::nullopt);
    }

    void EnableRealPathCache(TFileConf* refNames);

    const TFsPath& RealPathRoot(const TStringBuf& p) const;

    /// Translate absolute filesystem pathname into internal rooted path
    /// Sends event and returns abspath on failure
    TString CanonPath(const TStringBuf& abspath) const;

    /// Translate absolute filesystem pathname into internal rooted path
    /// This doesn't fill path if not succeed
    bool CanonPath(const TStringBuf& abspath, TString& path) const;

    TFsPath SourceRoot;
    TFsPath BuildRoot;
    TFsPath ArcadiaTestsDataRoot;
    bool NormalizeRealPath = false;

private:
    bool NeedNormalizeRealPath() const;
    bool IsRealPathCacheEnabled() const { return RefNames != nullptr; }
    TString RealPathByStr(TStringBuf p) const;

    mutable Y_THREAD(TVector<TString>) PathsCache;
    const TFileConf* RefNames = nullptr;
};
