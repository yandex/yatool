#pragma once

#include "name_store.h"
#include "name_data_store.h"

#include <devtools/ymake/compact_graph/dep_types.h>

#include <util/ysaveload.h>

struct TCommandData {
    TCommandData() noexcept
        : AllFlags{0}
    {}

    union {
        ui16 AllFlags; // @nodmp
        struct {
            ui8 CmdModStamp;      // for all command links
            ui8 KeepTargetPlatform:1; // ToolDeps of this command should be taken from target platform rather than host platform
        };
    };

    Y_SAVELOAD_DEFINE(AllFlags);
};

static_assert(sizeof(TCommandData) == sizeof(ui16), "union part of TCommandData must fit 16 bit");

using TCmdId = ui32;

class TVersionedCmdId {
public:
    explicit TVersionedCmdId(TCmdElemId elemId): ElemId_(elemId) {
    }
    TVersionedCmdId(TCmdId cmdId, bool isNewFormat): ElemId_(cmdId) {
        if (isNewFormat)
            ElemId_ = TCmdElemId(RawElemId(ElemId_) | NEW_FORMAT_MASK);
    }
    TCmdElemId ElemId() const {
        return ElemId_;
    }
    bool IsNewFormat() const {
        return RawElemId(ElemId_) & NEW_FORMAT_MASK;
    }
    TCmdId CmdId() const {
        return RawElemId(ElemId_) & ~NEW_FORMAT_MASK;
    }

private:
    constexpr static TElemId_Underlying NEW_FORMAT_MASK = (1 << 31);
    TCmdElemId ElemId_;
};

class TCmdView {
private:
    const TNameStore* Table;
    TVersionedCmdId Id;

public:
    TCmdView() : Table(nullptr), Id(TCmdElemId())
    {}

    TCmdView(const TNameStore* table, TCmdElemId elemId)
        : Table(table), Id(elemId)
    {}

    // TCmdView(const TNameStore* table, TElemId_Underlying elemId) // TODO make this unnecessary
    //     : Table(table), Id(TCmdElemId(elemId))
    // {}

    TStringBuf GetStr() const;

    TCmdElemId GetElemId() const {
        return Id.ElemId();
    }

    void SetElemId(TCmdElemId elemId) {
        Id = TVersionedCmdId(elemId);
    }

    TCmdId GetCmdId() const {
        return Id.CmdId();
    }

    bool HasId() const {
        return true;
    }

    bool IsValid() const;
    bool IsNewFormat() const {
        return Id.IsNewFormat();
    }
    bool operator==(const TCmdView& view) const;
    std::strong_ordering operator<=>(const TCmdView& view) const;
};

IOutputStream& operator<<(IOutputStream& out, TCmdView view);

class TCmdConf : public TNameDataStore<TCommandData, TCmdView> {
private:
    using TBase = TNameDataStore<TCommandData, TCmdView>;

public:
    TCmdElemId Add(TStringBuf name);

    TCmdView GetName(TCmdElemId elemId) const;
    TCmdView GetStoredName(TStringBuf name);

    TCmdElemId GetIdNx(TStringBuf name) const;
    TCmdElemId GetId(TStringBuf name) const;
};
