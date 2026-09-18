#include "module_add_data.h"


TModAddData::TModAddData(const TModAddData& o)
    : ActionData(o.ActionData)
    , AllFlags(o.AllFlags)
{
    if (o.ParsedPeerdirs) {
        ParsedPeerdirs = MakeHolder<THashSet<TFileElemId>>(*o.ParsedPeerdirs);
    }
}

TModAddData::~TModAddData() = default;

bool TModAddData::IsParsedPeer(TFileElemId elemId) const {
    return ParsedPeerdirs && ParsedPeerdirs->contains(elemId);
}
