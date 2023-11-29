#include "sortedreaddir.h"

#include <devtools/ymake/common/cyclestimer.h>
#include <devtools/ymake/diag/display.h>

#include <util/generic/algorithm.h>
#include <util/string/builder.h>

TSortedReadDir::TSortedReadDir() {
    Basenames_.reserve(16 * 1024);
}

TSortedReadDir::TDirItems& TSortedReadDir::ReadDir(const TString& fullPath, bool forceStat, NStats::TFileConfStats& stats) {
    Clear();
    try {
        TCyclesTimer openTimer;
        TReadDir readdir(fullPath, TReadDir::TOptions().SetForceStat(forceStat));
        auto openUs = openTimer.GetUs();
        SumUsStat(stats, openUs, NStats::EFileConfStats::OpendirCount, NStats::EFileConfStats::OpendirSumUs, NStats::EFileConfStats::OpendirMinUs, NStats::EFileConfStats::OpendirMaxUs);
        TCyclesTimer readTimer;
        for (auto [filename, isDir, stat]: readdir) {
            auto readUs = readTimer.GetUs();
            TCyclesTimerRestarter readTimerRestarter(readTimer);
            SumUsStat(stats, readUs, NStats::EFileConfStats::ReaddirCount, NStats::EFileConfStats::ReaddirSumUs, NStats::EFileConfStats::ReaddirMinUs, NStats::EFileConfStats::ReaddirMaxUs);
            AddItem(filename, isDir, 0, stat);
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

bool TSortedReadDir::IsReadFailed() const {
    return !ReadFailedMessage_.Empty();
}

TStringBuf TSortedReadDir::ReadFailedMessage() const {
    return ReadFailedMessage_;
}

void TSortedReadDir::AddItem(const TStringBuf basename, bool isDir, ui32 elemId, const TFileStat* stat) {
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
