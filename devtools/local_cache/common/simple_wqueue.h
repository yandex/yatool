#pragma once

#include <library/cpp/cache/cache.h>
#include <library/cpp/containers/stack_vector/stack_vec.h>
#include <library/cpp/deprecated/atomic/atomic.h>

#include <util/folder/path.h>
#include <util/generic/list.h>

#include <mutex>
#include <thread>
#include <condition_variable>

template <typename Item>
struct TChecker;

/// It is guaranteed that is one adds >= maxSize processes then there will be eventually
/// maxSize non-empty PopDeadProcess.
template <typename Item, typename Aux, typename Check = TChecker<Item>>
class TWalker : TNonCopyable {
public:
    using TVecOut = TStackVec<std::pair<Item, Aux>, 64>;
    enum EAddPolicy {
        UpdatePostion,
        NoPositionUpdate
    };

    TWalker(size_t maxSize, Check&& check)
        : MaxSize_(maxSize)
        , Cache_(maxSize)
        , SizeOfCache_(0)
        , LostItems_(0)
        , ShutdownSignaled_(0)
        , Checker_(std::move(check))
    {
    }
    ~TWalker() {
        Finalize();
    }
    /// Starts thread.
    void Initialize();
    /// Stops thread.
    void Finalize() noexcept;
    /// Removed item from cache.
    bool RemoveItem(Item item);
    /// Check item no promote.
    bool CheckNoPromote(Item item);
    /// Adds item to poll/process.
    bool AddItem(Item item, Aux id, EAddPolicy udpatePosition = UpdatePostion);
    /// Bulk insertion.
    template <typename Container>
    size_t AddItems(const Container& c, EAddPolicy udpatePosition = UpdatePostion);
    /// Pops single item to post-process.
    TMaybe<std::pair<Item, Aux>> PopOneProcessed();
    /// Pops vector of item to post-process.
    TVecOut PopVecProcessed(int limit = 64);
    /// Number of items polled/processed.
    size_t Size() const noexcept;
    /// Number of items to poll
    size_t TotalSize() noexcept;
    /// Number of items lost due to limited capacity.
    TAtomic CountLostItems() const noexcept {
        return AtomicGet(LostItems_);
    }
    TAtomic ResetLostItems() noexcept {
        return AtomicSwap(&LostItems_, 0);
    }
    /// Flush both Finished_ and Cache_
    size_t Flush();

    template <typename Output>
    void Out(Output& log);

private:
    using TElem = std::pair<Item, Aux>;
    using TCache = TLRUCache<Item, Aux>;

    /// Returns on signal and when there's no work.
    void WorkLoop();

    /// Single pass in work loop
    bool ProcessChunk();

private:
    size_t MaxSize_;
    /// Map from item to details.
    /// Multiple-producers, single-consumer.
    TCache Cache_;
    /// Finished work
    /// There could be duplicates, but it should happen infrequently due to async.
    /// Single-producer, multiple-consumers.
    TList<TElem> Finished_;

    /// WorkLoop
    std::thread WorkThread_;
    /// Lock concurrent accesses for WorkLoop Cache.
    std::mutex Locker_;
    std::condition_variable WaitFlag_;

    /// Cache.Size().
    TAtomic SizeOfCache_;
    /// Items lost due to limited size.
    TAtomic LostItems_;
    /// Shutdown signal.
    TAtomic ShutdownSignaled_;

    /// Checks if done and/or need post-process.
    Check Checker_;
};

#include "simple_wqueue-impl.h"
