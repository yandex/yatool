#include "cmd_store.h"

namespace {
    constexpr static TStringBuf NEW_FORMAT_PREFIX = "S:";

    bool IsNewFormatCmd(TStringBuf name) {
        return name.StartsWith(NEW_FORMAT_PREFIX);
    }
}

IOutputStream& operator<<(IOutputStream& out, TCmdView view) {
    out << view.GetStr();
    return out;
}

TStringBuf TCmdView::GetStr() const {
    if (!Table) {
        return {};
    }
    return Table->GetStringBufName(Id.CmdId());
}

bool TCmdView::IsValid() const {
    if (!Table) {
        return false;
    }
    return Table->CheckId(Id.CmdId());
}

bool TCmdView::operator==(const TCmdView& view) const {
    return Table == view.Table && Id.ElemId() == view.Id.ElemId();
}

std::strong_ordering TCmdView::operator<=>(const TCmdView& view) const {
    if (Table == view.Table && Id.ElemId() == view.Id.ElemId()) {
        return std::strong_ordering::equal;
    }

    return GetStr() < view.GetStr() ? std::strong_ordering::less : std::strong_ordering::greater;
}

ui32 TCmdConf::Add(TStringBuf name) {
    TVersionedCmdId id(TBase::Add(name), IsNewFormatCmd(name));
    return id.ElemId();
}

TCmdView TCmdConf::GetName(ui32 elemId) const {
    TVersionedCmdId id(elemId);
    auto target = TBase::GetName(id.CmdId());
    if (id.IsNewFormat())
        if (target.IsValid())
            target.SetElemId(elemId);
    return target;
}

TCmdView TCmdConf::GetStoredName(TStringBuf name) {
    ui32 id = Add(name);
    return GetName(id);
}

ui32 TCmdConf::GetIdNx(TStringBuf name) const {
    auto id = TBase::GetIdNx(name);
    if (!id)
        return 0;
    return TVersionedCmdId(id, IsNewFormatCmd(name)).ElemId();
}

ui32 TCmdConf::GetId(TStringBuf name) const {
    auto id = NameStore.GetId(name);
    return TVersionedCmdId(id, IsNewFormatCmd(name)).ElemId();
}
