#include "sortedreaddir.h"
#include "fuse_prefetch.h"

#include <atomic>
#include <cstring>
#include <mutex>
#ifndef _win_
#include <fcntl.h>
#include <sys/stat.h>
#endif

#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/libs/clocks/checkpoint.h>
#include <devtools/ymake/libs/clocks/hp_clock.h>

#include <util/generic/algorithm.h>
#include <util/string/builder.h>

TSortedReadDir::TSortedReadDir() {
    Basenames_.reserve(16 * 1024);
}

#ifdef _win_
void TSortedReadDir::ParallelStat(const TString&, NStats::TFileConfStats&) {
}
#else
void TSortedReadDir::ParallelStat(const TString& fullPath, NStats::TFileConfStats& stats) {
    const auto checkpoint = MakeCheckpoint<THPClock>();
    int dirFd = open(fullPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirFd < 0) {
        ythrow TFileError() << "Can't open directory '" << fullPath << "' by open()";
    }
    std::mutex failedLock;
    int failedErrno = 0;
    TString failedName;
    std::atomic<size_t> slowStats = 0;
    const auto statItem = [&](TDirItem& item) {
        if (item.Stat) {
            return;
        }
        const auto statCheckpoint = MakeCheckpoint<THPClock>();
        struct stat st;
        if (fstatat(dirFd, TString(GetBasename(item)).c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0) {
            // Same as TReadDir: a symlink gets no stat
            if (!S_ISLNK(st.st_mode)) {
                item.Stat = TFileStat(st);
            }
        } else {
            const int err = errno;
            std::lock_guard g(failedLock);
            if (!failedErrno) {
                failedErrno = err;
                failedName = GetBasename(item);
            }
        }
        if (TimeSince(statCheckpoint) > SlowStatThreshold_) {
            ++slowStats;
        }
    };
    size_t next = 0;
    if (!SlowStat_ && next < DirItems_.size()) {
        statItem(DirItems_[next++]);
        SlowStat_ = slowStats > 0;
    }
    if (SlowStat_) {
        const size_t rest = DirItems_.size() - next;
        slowStats = 0;
        NFusePrefetch::ParallelFor(rest, [&](size_t i) {
            statItem(DirItems_[next + i]);
        });
        SlowStat_ = 2 * slowStats >= rest;
    } else {
        for (; next < DirItems_.size(); ++next) {
            statItem(DirItems_[next]);
        }
    }
    close(dirFd);
    if (failedErrno) {
        errno = failedErrno;
        ythrow TFileError() << "Can't lstat() file '" << fullPath << '/' << failedName << "' by fstatat() (" << failedErrno << " - " << strerror(failedErrno) << ")";
    }
    stats.Inc(NStats::EFileConfStats::ReaddirSumUs, std::chrono::duration_cast<std::chrono::microseconds>(TimeSince(checkpoint)).count());
}
#endif

TSortedReadDir::TDirItems& TSortedReadDir::ReadDir(const TString& fullPath, bool forceStat, NStats::TFileConfStats& stats) {
    Clear();
    const bool parallelStat = forceStat && ParallelStatEnabled_;
    try {
        const auto openCheckpoint = MakeCheckpoint<THPClock>();
        TReadDir readdir(fullPath, TReadDir::TOptions().SetForceStat(forceStat && !parallelStat));
        const auto openTime = std::chrono::duration_cast<std::chrono::microseconds>(TimeSince(openCheckpoint));
        SumUsStat(stats, openTime.count(), NStats::EFileConfStats::OpendirCount, NStats::EFileConfStats::OpendirSumUs, NStats::EFileConfStats::OpendirMinUs, NStats::EFileConfStats::OpendirMaxUs);
        auto readCheckpoint = MakeCheckpoint<THPClock>();
        for (auto [filename, isDir, stat]: readdir) {
            auto readUs = std::chrono::duration_cast<std::chrono::microseconds>(TimeSince(readCheckpoint));
            SumUsStat(stats, readUs.count(), NStats::EFileConfStats::ReaddirCount, NStats::EFileConfStats::ReaddirSumUs, NStats::EFileConfStats::ReaddirMinUs, NStats::EFileConfStats::ReaddirMaxUs);
            AddItem(filename, isDir, TFileElemId(), stat);
            readCheckpoint = MakeCheckpoint<THPClock>();
        }
        if (parallelStat) {
            ParallelStat(fullPath, stats);
        }
    } catch (const TReadDir::TError& error) {
        ReadFailedMessage_ = TStringBuilder() << "Can't read directory content: " << error.what();
        YWarn() << ReadFailedMessage_ << Endl;
    } catch (const TFileError& error) {
        ReadFailedMessage_ = TStringBuilder() << "Can't read directory content: " << error.what() << " (" << error.Status() << ")";
        YWarn() << ReadFailedMessage_ << Endl;
    }
    ResortDirItems();
    return DirItems_;
}

void TSortedReadDir::SetParallelStat(bool enabled) {
#ifndef _win_
    ParallelStatEnabled_ = enabled;
#else
    Y_UNUSED(enabled);
#endif
}

void TSortedReadDir::SetSlowStatThreshold(std::chrono::microseconds threshold) {
    SlowStatThreshold_ = threshold;
}

bool TSortedReadDir::IsReadFailed() const {
    return !ReadFailedMessage_.empty();
}

TStringBuf TSortedReadDir::ReadFailedMessage() const {
    return ReadFailedMessage_;
}

void TSortedReadDir::AddItem(const TStringBuf basename, bool isDir, TFileElemId elemId, const TFileStat* stat) {
    auto basenameBeg = Basenames_.size();
    Basenames_.append(basename);
    DirItems_.emplace_back(TDirItem{
        .BasenameBeg = basenameBeg,
        .BasenameSize = basename.size(),
        .ElemId = elemId,
        .IsDir = isDir,
        .Stat = stat ? std::make_optional(*stat) : std::optional<TFileStat>{}
    });
}

void TSortedReadDir::ResortDirItems() {
    Sort(DirItems_, [&](const TDirItem& lhs, const TDirItem& rhs) {
        return GetBasename(lhs) < GetBasename(rhs);
    });
}

TStringBuf TSortedReadDir::GetBasename(const TDirItem& dirItem) const {
    return TStringBuf(Basenames_.data() + dirItem.BasenameBeg, dirItem.BasenameSize);
}

bool TSortedReadDir::IsStatSlow() const {
    return SlowStat_;
}

void TSortedReadDir::Clear() {
    Basenames_.clear();
    DirItems_.clear();
    ReadFailedMessage_.clear();
}

void TSortedReadDir::ClearAndFree() {
    Clear();
    Basenames_.shrink_to_fit();
    DirItems_.shrink_to_fit();
}

void TSortedReadDir::SumUsStat(NStats::TFileConfStats& stats, size_t us, NStats::EFileConfStats countStat, NStats::EFileConfStats sumUsStat, NStats::EFileConfStats minUsStat, NStats::EFileConfStats maxUsStat) {
    stats.Inc(countStat);
    stats.Inc(sumUsStat, us);
    stats.SetMin(minUsStat, us);
    stats.SetMax(maxUsStat, us);
}
