#pragma once

#include "readdir.h"

#include <devtools/ymake/diag/stats.h>

#include <util/generic/vector.h>

#include <optional>
#include <string>

class TSortedReadDir {
public:
    struct TDirItem {
        size_t BasenameBeg;
        size_t BasenameSize;
        ui32 ElemId;
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

    void AddItem(const TStringBuf basename, bool isDir, ui32 elemId = 0, const TFileStat* stat = nullptr);
    void ResortDirItems();
    TStringBuf GetBasename(const TDirItem& dirItem) const;

    void Clear();
    void ClearAndFree();

private:
    std::string Basenames_;
    TDirItems DirItems_;
    TString ReadFailedMessage_;

    static void SumUsStat(NStats::TFileConfStats& stats, size_t us,
        NStats::EFileConfStats countStat,
        NStats::EFileConfStats sumUsStat,
        NStats::EFileConfStats minUsStat,
        NStats::EFileConfStats maxUsStat
    );
};
