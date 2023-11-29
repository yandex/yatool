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
    explicit TVersionedCmdId(ui32 elemId): ElemId_(elemId) {
    }
    TVersionedCmdId(TCmdId cmdId, bool isNewFormat): ElemId_(cmdId) {
        if (isNewFormat)
            ElemId_ |= NEW_FORMAT_MASK;
    }
    ui32 ElemId() const {
        return ElemId_;
    }
    bool IsNewFormat() const {
        return ElemId_ & NEW_FORMAT_MASK;
    }
    TCmdId CmdId() const {
        return ElemId_ & ~NEW_FORMAT_MASK;
    }

private:
    constexpr static ui32 NEW_FORMAT_MASK = (1 << 31);
    ui32 ElemId_;
};

class TCmdView {
private:
    const TNameStore* Table;
    TVersionedCmdId Id;

public:
    TCmdView() : Table(nullptr), Id(0)
    {}

    TCmdView(const TNameStore* table, ui32 elemId)
        : Table(table), Id(elemId)
    {}

    TStringBuf GetStr() const;

    ui32 GetElemId() const {
        return Id.ElemId();
    }

    void SetElemId(ui32 elemId) {
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
    ui32 Add(TStringBuf name);

    TCmdView GetName(ui32 elemId) const;
    TCmdView GetStoredName(TStringBuf name);

    ui32 GetIdNx(TStringBuf name) const;
    ui32 GetId(TStringBuf name) const;
};
