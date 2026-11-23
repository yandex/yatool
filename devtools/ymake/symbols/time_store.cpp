#include "time_store.h"
#include "util/system/yassert.h"

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/dep_types.h>

#include <devtools/ymake/diag/dbg.h>

#include <util/folder/path.h>

#include <ctime>

#ifdef _win_
#include <time.h>
#endif

struct TTimeStamps::TCntForPos {
    ui8 Pos;
    size_t Count;

    TCntForPos(size_t pos)
        : Pos(pos)
        , Count(0)
    {
    }

    // Pos == 0 -> greater than all
    bool operator<(const TCntForPos& than) const {
        return Pos && Count < than.Count || Count == than.Count && Pos > than.Pos;
    }

    static bool ByPos(const TCntForPos& a, const TCntForPos& b) {
        return a.Pos < b.Pos;
    }
};

struct TTimeStamps::TStampMoveDesc {
    bool CanMoveLeft;
    union {
        ui8 MoveLeftTo;
        ui8 RemapTo;
    };
    bool CanMoveRight;
    ui8 MoveRightTo;

    TStampMoveDesc() noexcept {
        Zero(*this);
    }

    bool NeedMove() const {
        return CanMoveLeft || CanMoveRight;
    }

    void SetLeft(ui8 left) {
        CanMoveLeft = true;
        MoveLeftTo = left;
    }

    void SetRight(ui8 right) {
        CanMoveRight = true;
        MoveRightTo = right;
    }

    void Print(size_t n, i64 t) {
        YDIAG(DG) << "CT[" << n << "] (t=" << t << ")";
        if (CanMoveLeft) {
            YDIAG(DG) << " " << (ui32)MoveLeftTo << " <- ";
        }
        if (NeedMove()) {
            YDIAG(DG) << "*";
        } else {
            YDIAG(DG) << " " << (ui32)RemapTo;
        }
        if (CanMoveRight) {
            YDIAG(DG) << " -> " << (ui32)MoveRightTo;
        }
        YDIAG(DG) << Endl;
    }
};

struct TTimeStamps::TMoveMap: public std::array<TTimeStamps::TStampMoveDesc, 256> {
    // this function changes some dates to newer ones
    void MoveStamp(ui8& stamp, size_t& mvStats) {
        if (stamp) {
            if ((*this)[stamp].NeedMove()) {
                Y_ASSERT((*this)[stamp].CanMoveRight); // this holds when KeepLast > 0
                stamp = (*this)[stamp].MoveRightTo;
                mvStats++;
            } else {
                stamp = (*this)[stamp].RemapTo;
            }
        }
    }

    // this function changes some dates to older ones (including 0)
    void MoveStampLeft(ui8& stamp, size_t& mvStats) {
        if (stamp) {
            if ((*this)[stamp].NeedMove()) {
                Y_ASSERT((*this)[stamp].CanMoveLeft);
                stamp = (*this)[stamp].MoveLeftTo;
                mvStats++;
            } else {
                stamp = (*this)[stamp].RemapTo;
            }
        }
    }

    // this function deletes some dates (i.e. sets to 0)
    void FixStamp(ui8& stamp, size_t& delStats) {
        if (!stamp || (*this)[stamp].NeedMove()) {
            stamp = 0;
            delStats++;
        } else {
            stamp = (*this)[stamp].RemapTo;
        }
    }
};

void TTimeStamps::CompressTimes(THashMap<TElemId, TNodeData>& nodeData) {
    if (NumTimes <= SingleDrop) {
        return;
    }
    YDIAG(DG) << "In CompressTimes\n";
    TVector<TCntForPos> useCount(Reserve(NumTimes));
    for (size_t pos = 0; pos < NumTimes; ++pos)
        useCount.push_back({pos});

    // we want to delete least used date stamps
    const auto fileHistogram = Elems.FileConf.StampHistogram();
    for (size_t stamp = 0; stamp < fileHistogram.size(); ++stamp) {
        Y_ENSURE(!fileHistogram[stamp] || stamp < NumTimes, "Invalid file modification stamp");
        if (stamp < NumTimes) {
            useCount[stamp].Count = fileHistogram[stamp];
        }
    }
    Y_ASSERT(KeepLast >= 1);
    DoSwap(useCount[0], useCount[useCount.size() - 1 - KeepLast]);    // 0 is a reserved 'no stamp' stamp
    std::sort(useCount.begin(), useCount.end() - 1 - KeepLast); // 0 and last KeepLast stamps will not be deleted
    std::sort(useCount.begin(), useCount.begin() + SingleDrop, TCntForPos::ByPos);
    size_t numFiles = 0;

    TTimeLong nTimes[256];
    memset(nTimes, 0, sizeof(nTimes));
    // Nodes 0 .. SingleDrop-1 will be deleted.
    // We get left-most non-deleted (if any) and right-most non-deleted index
    TMoveMap moveMap;
    size_t rFrom = 0, rTo = 0;
    for (size_t n = 0; n < SingleDrop; n++) {
        numFiles += useCount[n].Count;
        ui8 pos = useCount[n].Pos;

        for (; rFrom < pos; rFrom++) { // non-deleted stamps are mapped
            nTimes[rTo] = Times[rFrom];
            moveMap[rFrom].RemapTo = rTo++;
        }
        rFrom = (size_t)pos + 1;

        Y_ASSERT(rTo > 0); // 0'th time stamp is never deleted
        moveMap[pos].SetLeft(rTo - 1);
        if (rTo != NumTimes - SingleDrop) {
            moveMap[pos].SetRight(rTo);
        }
    }
    Y_ASSERT(rTo + SingleDrop != NumTimes);
    for (; rFrom < NumTimes; rFrom++) {
        nTimes[rTo] = Times[rFrom];
        moveMap[rFrom].RemapTo = rTo++;
    }
    Y_ASSERT(rTo + SingleDrop == NumTimes);

    YDIAG(DG) << "TTimeStamps: compress by pulling " << numFiles << " files\n";

    for (size_t n = 0; n < NumTimes /*256*/; n++)
        moveMap[n].Print(n, Times[n]);

    size_t rescanFileCont = 0, rescanNodeCont = 0, recmdCount = 0;
    for (auto& [elemId, data] : nodeData) {
        moveMap.MoveStampLeft(data.NodeModStamp, rescanNodeCont);
    }

    TMetadataStampRemap remap;
    const auto commandHistogram = Elems.CommandConf.StampHistogram();
    for (size_t stamp = 0; stamp < 256; ++stamp) {
        size_t ignored = 0;
        moveMap.FixStamp(remap.Checked[stamp], ignored);
        // Never (255) is legal for LastCheckedStamp, not modification stamps.
        if (stamp < NumTimes) {
            moveMap.MoveStamp(remap.Modified[stamp], ignored);
            if (stamp && moveMap[stamp].NeedMove()) {
                rescanFileCont += fileHistogram[stamp];
                recmdCount += commandHistogram[stamp];
            }
        } else {
            Y_ENSURE(!commandHistogram[stamp], "Invalid command modification stamp");
        }
    }
    Elems.FileConf.RemapStamps(remap);
    Elems.CommandConf.RemapStamps(remap);

    if (rescanFileCont || rescanNodeCont || recmdCount) {
        YDIAG(DG) << "TTimeStamps: compress spoiled the state of " << rescanFileCont << " files, " << rescanNodeCont << " nodes and " << recmdCount << " commands\n";
    }

    NumTimes = rTo;
    memcpy(Times, nTimes, sizeof(nTimes));
}

void TTimeStamps::InitSession(THashMap<TElemId, TNodeData>& nodeData) {

    if (!NeedNewSession) {
        return;
    }
    NeedNewSession = false;
    Times[NumTimes++] = time(nullptr);
    if (Y_UNLIKELY(NumTimes >= FillMax)) {
        CompressTimes(nodeData);
    }
}

void TTimeStamps::Save(TMultiBlobBuilder& builder) {
    builder.AddBlob(new TBlobSaverMemory(Times, NumTimes * sizeof(*Times)));
}

void TTimeStamps::Load(TBlob& store) {
    NumTimes = store.Size() / sizeof(*Times);
    if (store.Size() % sizeof(*Times) || store.Size() > sizeof(Times)) {
        ythrow yexception() << "Trying to initialize TTimeStamps from a blob of incorrect size:" << store.Size();
    }
    memcpy(Times, store.Data(), store.Size());
}
