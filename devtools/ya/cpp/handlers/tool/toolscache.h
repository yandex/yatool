#pragma once

#include <devtools/local_cache/toolscache/dbbei.h>
#include <devtools/ya/cpp/lib/config.h>

#include <util/generic/string.h>
#include <util/folder/path.h>


namespace NYa::NTool {
    class TToolsCache : TNonCopyable {
    public:
        explicit TToolsCache(const IConfig& config);
        ~TToolsCache();

        void Notify(const TFsPath& toolChainPath);
        void Lock(const TFsPath& toolChainPath);
    private:
        void InsertResource(const TFsPath& toolChainPath);
        void LockResource(const TFsPath& toolChainPath);
    private:
        THolder<NToolsCache::TToolsCacheDBBE> Dbbe_;
        TFsPath ToolRoot_;
    };
}
