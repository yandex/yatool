#include "toolscache.h"

#include <devtools/ya/cpp/lib/config.h>
#include <devtools/ya/cpp/lib/logger.h>
#include <devtools/local_cache/toolscache/dbbei.h>

#include <util/system/getpid.h>


namespace NYa::NTool {
    const TFsPath DB_FILE{"tcdb.sqlite"};

    TToolsCache::TToolsCache(const IConfig& config) {
        if (config.ToolCacheVersion() == 4) {
            long long gcLimit = 20LL * 1024LL * 1024LL * 1024LL;
            THolder<IThreadPool> pool(CreateThreadPool(2));
            TLog& log = GetLog();
            auto procMaxQueue = 0;
            auto gcMaxQueue = 0;
            auto quiescenceTime = 1; // 1ms

            auto errorHandler = [](TLog&, const std::exception& e) -> void {
                WARNING_LOG << "Tools cache operation failed with critical error: " << e.what() << "\n";
            };

            ToolRoot_ = config.ToolRoot();
            TFsPath dbPath = ToolRoot_ / DB_FILE;
            if (dbPath.Exists()) {
                try {
                    Dbbe_ = MakeHolder<NToolsCache::TToolsCacheDBBE>(dbPath.GetPath(), gcLimit, *pool, log, errorHandler, procMaxQueue, gcMaxQueue, quiescenceTime);
                } catch (const yexception& e) {
                    WARNING_LOG << "Tools cache DB-backend failed with error: " << e.what() << "\n";
                }
            } else {
                WARNING_LOG << "Tools cache database doesn't exist: " << dbPath << "\n";
            }

        }
    }

    TToolsCache::~TToolsCache() {
        if (Dbbe_) {
            Dbbe_->Finalize();
        }
    }

    void TToolsCache::Notify(const TFsPath& toolChainPath) {
        if (Dbbe_) {
            try {
                InsertResource(toolChainPath);
                if (toolChainPath.IsSymlink()) {
                    InsertResource(toolChainPath.ReadLink());
                }
            } catch (const yexception& e) {
                WARNING_LOG << "InsertResource() failed with error: " << e.what() << "\n";
            }
        }
    }

    void TToolsCache::Lock(const TFsPath& toolChainPath) {
        if (Dbbe_) {
            try {
                LockResource(toolChainPath);
                if (toolChainPath.IsSymlink()) {
                    LockResource(toolChainPath.ReadLink());
                }
            } catch (const yexception& e) {
                WARNING_LOG << "LockResource() failed with error: " << e.what() << "\n";
            }
        }
    }

    void TToolsCache::InsertResource(const TFsPath& toolChainPath) {
        NToolsCache::TResourceUsed resourceUsed{};
        resourceUsed.MutableResource()->SetPath(toolChainPath.Dirname().c_str());
        resourceUsed.MutableResource()->SetSBId(toolChainPath.Basename().c_str());
        resourceUsed.MutablePeer()->MutableProc()->SetPid(GetPID());
        resourceUsed.MutablePeer()->MutableProc()->SetStartTime(TInstant::Now().Seconds());
        resourceUsed.MutablePeer()->MutableProc()->SetExpectedLifeTime(1);
        Dbbe_->InsertResource(resourceUsed);
    }

    void TToolsCache::LockResource(const TFsPath& toolChainPath) {
        try {
            NToolsCache::TSBResource sbResource{};
            sbResource.SetPath(toolChainPath.Dirname().c_str());
            sbResource.SetSBId(toolChainPath.Basename().c_str());
            Dbbe_->LockResource(sbResource);
        } catch (const yexception& e) {
            WARNING_LOG << "LockResource() failed with error: " << e.what() << "\n";
        }
    }
}
