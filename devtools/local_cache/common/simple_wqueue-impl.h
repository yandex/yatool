#pragma once

#include "simple_wqueue.h"
#include <util/system/spin_wait.h>

template <typename Item, typename Aux, typename Check>
inline void TWalker<Item, Aux, Check>::WorkLoop() {
    TSpinWait sw;

    while (ProcessChunk()) {
        sw.Sleep();
    }
}

template <typename Item, typename Aux, typename Check>
inline bool TWalker<Item, Aux, Check>::ProcessChunk() {
    Aux aux;
    Item key;

    {
        std::unique_lock<std::mutex> lock(Locker_);
        WaitFlag_.wait(lock, [this] { return SizeOfCache_ > 0 || ShutdownSignaled_; });

        if (ShutdownSignaled_) {
            return false;
        }

        auto it = Cache_.FindOldest();
        key = it.Key();
        Y_ABORT_UNLESS(Cache_.PickOut(key, &aux));
        AtomicDecrement(SizeOfCache_);
    }

    /// key and aux are allowed to be changed
    auto [done, needsPostProcess] = Checker_.Process(key, aux);
    Y_ABORT_UNLESS(Checker_.Verify(key));

    // Cache update part.
    {
        std::unique_lock<std::mutex> lock(Locker_);

        if (done) {
            if (needsPostProcess) {
                if (Finished_.size() < MaxSize_) {
                    Finished_.emplace_back(key, aux);
                } else {
                    AtomicIncrement(LostItems_);
                }
            }
        } else {
            auto size = Cache_.Size();
            bool evicted = Cache_.Insert(key, aux);
            auto inc = Cache_.Size() - size;
            Y_ABORT_UNLESS(inc == 0 || inc == 1);
            if (inc) {
                AtomicIncrement(SizeOfCache_);
            } else if (evicted) {
                AtomicIncrement(LostItems_);
            }
        }
    }
    return true;
}

template <typename Item, typename Aux, typename Check>
inline bool TWalker<Item, Aux, Check>::AddItem(Item item, Aux id, EAddPolicy updatePosition) {
    size_t inc = 0;
    {
        std::unique_lock<std::mutex> lock(Locker_);

        Y_ABORT_UNLESS(Checker_.Verify(item));
        if (updatePosition == NoPositionUpdate && Cache_.FindWithoutPromote(item) != Cache_.End()) {
            return inc > 0;
        }

        auto size = Cache_.Size();
        bool evicted = Cache_.Insert(item, id);
        inc = Cache_.Size() - size;

        Y_ABORT_UNLESS(inc == 0 || inc == 1);
        if (inc) {
            AtomicIncrement(SizeOfCache_);
        } else if (evicted) {
            AtomicIncrement(LostItems_);
        }
    }
    WaitFlag_.notify_one();
    return inc > 0;
}

template <typename Item, typename Aux, typename Check>
inline bool TWalker<Item, Aux, Check>::RemoveItem(Item item) {
    size_t inc = 0;
    {
        std::unique_lock<std::mutex> lock(Locker_);

        auto size = Cache_.Size();
        auto it = Cache_.Find(item);
        if (it == Cache_.End()) {
            return false;
        }
        Cache_.Erase(it);
        inc = size - Cache_.Size();

        Y_ABORT_UNLESS(inc == 0 || inc == 1);
        if (inc) {
            AtomicAdd(SizeOfCache_, -1);
        }
    }
    WaitFlag_.notify_one();
    return inc > 0;
}

template <typename Item, typename Aux, typename Check>
inline bool TWalker<Item, Aux, Check>::CheckNoPromote(Item item) {
    std::unique_lock<std::mutex> lock(Locker_);
    auto it = Cache_.FindWithoutPromote(item);
    return it != Cache_.End();
}

template <typename Item, typename Aux, typename Check>
template <typename Container>
inline size_t TWalker<Item, Aux, Check>::AddItems(const Container& c, EAddPolicy updatePosition) {
    size_t old_size = 0;
    size_t new_size = 0;
    {
        std::unique_lock<std::mutex> lock(Locker_);

        auto prev_size = Cache_.Size();
        old_size = prev_size;
        for (auto& i : c) {
            Y_ABORT_UNLESS(Checker_.Verify(i.first));

            if (updatePosition == NoPositionUpdate && Cache_.FindWithoutPromote(i.first) != Cache_.End()) {
                continue;
            }

            bool evicted = Cache_.Insert(i.first, i.second);
            auto new_size = Cache_.Size();

            auto inc = new_size - prev_size;
            Y_ABORT_UNLESS(inc == 0 || inc == 1);
            if (inc) {
                AtomicIncrement(SizeOfCache_);
            } else if (evicted) {
                AtomicIncrement(LostItems_);
            }
            prev_size = new_size;
        }
        new_size = prev_size;
    }
    WaitFlag_.notify_one();
    return new_size - old_size;
}

template <typename Item, typename Aux, typename Check>
inline TMaybe<std::pair<Item, Aux>> TWalker<Item, Aux, Check>::PopOneProcessed() {
    std::unique_lock<std::mutex> lock(Locker_);

    if (Finished_.empty()) {
        return TMaybe<std::pair<Item, Aux>>();
    }
    auto out = *Finished_.begin();

    Finished_.erase(Finished_.begin());
    Y_ABORT_UNLESS(out.first.GetStartTime() > 0);
    return MakeMaybe(out);
}

template <typename Item, typename Aux, typename Check>
inline typename TWalker<Item, Aux, Check>::TVecOut TWalker<Item, Aux, Check>::PopVecProcessed(int limit) {
    std::unique_lock<std::mutex> lock(Locker_);

    TVecOut out;

    auto it = Finished_.begin();
    size_t idx = 0;
    for (auto end = Finished_.end(); it != end && idx < size_t(limit); ++it, ++idx) {
        Y_ABORT_UNLESS(it->first.GetStartTime() > 0);
        out.emplace_back(*it);
    }
    Finished_.erase(Finished_.begin(), it);
    Y_ABORT_UNLESS(out.size() == idx);
    return out;
}

template <typename Item, typename Aux, typename Check>
inline size_t TWalker<Item, Aux, Check>::Flush() {
    std::unique_lock<std::mutex> lock(Locker_);
    auto sz = Cache_.Size() + Finished_.size();
    Cache_.Clear();
    Finished_.clear();
    AtomicSet(SizeOfCache_, 0);
    AtomicSet(LostItems_, 0);
    return sz;
}

template <typename Item, typename Aux, typename Check>
inline size_t TWalker<Item, Aux, Check>::TotalSize() noexcept {
    std::unique_lock<std::mutex> lock(Locker_);
    return Cache_.Size() + Finished_.size();
}

template <typename Item, typename Aux, typename Check>
inline size_t TWalker<Item, Aux, Check>::Size() const noexcept {
    return AtomicGet(SizeOfCache_);
}

template <typename Item, typename Aux, typename Check>
inline void TWalker<Item, Aux, Check>::Initialize() {
    std::unique_lock<std::mutex> lock(Locker_);
    if (!WorkThread_.joinable()) {
        WorkThread_ = std::thread([this]() { this->WorkLoop(); });
    }
}

template <typename Item, typename Aux, typename Check>
inline void TWalker<Item, Aux, Check>::Finalize() noexcept {
    {
        std::unique_lock<std::mutex> lock(Locker_);
        ShutdownSignaled_ = 1;
    }

    WaitFlag_.notify_one();
    if (WorkThread_.joinable()) {
        WorkThread_.join();
        WorkThread_ = std::thread();
    }

    {
        std::unique_lock<std::mutex> lock(Locker_);
        ShutdownSignaled_ = 0;
    }
}

template <typename Item, typename Aux, typename Check>
template <typename Output>
inline void TWalker<Item, Aux, Check>::Out(Output& log) {
    for (auto it = Cache_.Begin(), end = Cache_.End(); it != end; ++it) {
        log << "   " << it->ToString() << Endl;
    }
}
