#pragma once

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>
#include <library/cpp/sqlite3/sqlite.h>

#include <util/system/yield.h>

namespace NCachesPrivate {
    /// Wrapper to handle exceptions in ProcessWork.
    /// Provides interface for common/simple_wqueue.h and delegates implementation to WorkQueueImpl
    /// Errors are retried or delegated to ErrorHandler_. Should be careful to avoid deadlock.
    template <typename WorkQueueImpl, const char EmergencyMsg[]>
    class THandleExceptions : TMoveOnly {
    public:
        THandleExceptions(WorkQueueImpl* parent)
            : Parent_(parent)
        {
        }

        /// Relying that ProcessingThread_ does not hold locks during Process call.
        std::pair<bool, bool> Process(typename WorkQueueImpl::TWorkItem& item, typename WorkQueueImpl::TAux& aux) noexcept {
            try {
                bool done = Parent_->ProcessWork(item, aux);
                return std::make_pair(done, false /* no post-process */);
            } catch (const NSQLite::TSQLiteError& err) {
                auto rc = err.GetErrorCode();
                if (rc == SQLITE_LOCKED || rc == SQLITE_BUSY) {
                    SchedYield();
                    // Do not report in log.
                    AtomicIncrement(WorkQueueImpl::Interface::DBLocked);
                    return std::make_pair(false /* retry */, false /* no post-process */);
                }
                // Cannot stop Parent_->ProcessingThread_ from here without deadlock.
                Parent_->ErrorHandler_(Parent_->Log_, err);
                return std::make_pair(true /* drop */, false /* no post-process */);
            } catch (const std::exception& err) {
                // Cannot stop Parent_->ProcessingThread_ from here without deadlock.
                Parent_->ErrorHandler_(Parent_->Log_, err);
                return std::make_pair(true /* drop */, false /* no post-process */);
            } catch (...) {
                // No good mean to throw exception from separate thread. Hope Log_ is still usable.
                LOGGER_CHECKED_GENERIC_LOG(Parent_->Log_, TRTYLogPreprocessor, TLOG_EMERG, EmergencyMsg) << "UNKNOWN EXCEPTION for " << ToString(item) << ", aux:" << aux.ToString() << Endl;
                Y_ABORT_UNLESS(0);
            }
            Y_UNREACHABLE();
        }

        bool Verify(const typename WorkQueueImpl::TWorkItem& item) const {
            return Parent_->Verify(item);
        }

    protected:
        WorkQueueImpl* Parent_;
    };
}
