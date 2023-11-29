#pragma once

#include "devtools/local_cache/psingleton/proto/known_service.pb.h"

#include <library/cpp/deprecated/atomic/atomic.h>
#include <library/cpp/logger/log.h>
#include <library/cpp/sqlite3/sqlite.h>

namespace NCachesPrivate {
    using TCriticalErrorHandler = std::function<void(TLog&, const std::exception&)>;

    /// Started from main process (TIntegrityHandler, for example) responsible for insertion to DB.
    /// Scans running processes, checks and updates DB.
    ///
    /// DBHandler should specify type TSetupConnection to setup DB connection from TImpl ctor.
    /// GCAndFSHandler only required to have WakeUpGC method.
    ///
    /// May cycle forever if number of hung processes is greater than maxSize in ctor.
    template <typename GCAndFSHandler, typename DBHandler>
    class TRunningProcsHandler : TNonCopyable {
    public:
        TRunningProcsHandler(THolder<NSQLite::TSQLiteDB>&& db, GCAndFSHandler& gcHandler, TLog& log, const TCriticalErrorHandler& handler, int maxSize);
        ~TRunningProcsHandler();

        void Initialize();
        void Finalize() noexcept;
        void Flush();
        void AddRunningProcess(const NUserService::TProc& proc, i64 rowid);
        size_t GetWorkQueueSizeEstimation() const noexcept;

        void RemoveRunningProcess(const NUserService::TProc& proc);

    public:
        /// # of times request was blocked.
        static TAtomic DBLocked;

        /// # of times capacity was insufficient.
        static TAtomic CapacityIssues;

    private:
        class TImpl;
        THolder<TImpl> Ref_;
    };
}
