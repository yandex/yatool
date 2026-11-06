#pragma once

#include <chrono>
#include "readdir.h"
#include "elem_id.h"

#include <devtools/ymake/diag/stats.h>

#include <util/generic/vector.h>

#include <optional>
#include <string>

class TSortedReadDir {
public:
    struct TDirItem {
        size_t BasenameBeg;
        size_t BasenameSize;
        TFileElemId ElemId;
        bool IsDir;
        std::optional<TFileStat> Stat;
    };
    using TDirItems = TVector<TDirItem>;

public:
    TSortedReadDir();
    ~TSortedReadDir() = default;

    TDirItems& ReadDir(const TString& fullPath, bool forceStat, NStats::TFileConfStats& stats);
    bool IsReadFailed() const;
    TStringBuf ReadFailedMessage() const;
    bool IsStatSlow() const;
    // Parallel lstat in directories with slow lstat. Ignored on Windows.
    void SetParallelStat(bool enabled);
    void SetSlowStatThreshold(std::chrono::microseconds threshold);

    void AddItem(const TStringBuf basename, bool isDir, TFileElemId elemId = TFileElemId(), const TFileStat* stat = nullptr);
    void ResortDirItems();
    TStringBuf GetBasename(const TDirItem& dirItem) const;

    void Clear();
    void ClearAndFree();

private:
    std::string Basenames_;
    TDirItems DirItems_;
    TString ReadFailedMessage_;
    bool ParallelStatEnabled_ = false;
    bool SlowStat_ = false;
    std::chrono::microseconds SlowStatThreshold_{20};

    void ParallelStat(const TString& fullPath, NStats::TFileConfStats& stats);

    static void SumUsStat(NStats::TFileConfStats& stats, size_t us,
        NStats::EFileConfStats countStat,
        NStats::EFileConfStats sumUsStat,
        NStats::EFileConfStats minUsStat,
        NStats::EFileConfStats maxUsStat
    );
};
