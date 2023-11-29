#include "module_add_data.h"
#include "macro_processor.h"


TModAddData::TModAddData(const TModAddData& o)
    : CmdInfo(o.CmdInfo)
    , AllFlags(o.AllFlags)
{
    if (o.ParsedPeerdirs) {
        ParsedPeerdirs = MakeHolder<THashSet<ui32>>(*o.ParsedPeerdirs);
    }
}

TModAddData::~TModAddData() = default;

bool TModAddData::IsParsedPeer(ui32 elemId) const {
    return ParsedPeerdirs && ParsedPeerdirs->contains(elemId);
}
