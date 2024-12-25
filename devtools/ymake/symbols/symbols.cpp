#include "symbols.h"


#include <library/cpp/digest/crc32c/crc32c.h>
#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/retry/retry.h>

#include <util/digest/city.h>
#include <util/generic/algorithm.h>
#include <util/memory/blob.h>
#include <util/folder/dirut.h>
#include <util/folder/iterator.h>
#include <util/string/builder.h>
#include <util/string/vector.h>
#include <util/system/yassert.h>


TFileView TSymbols::FileNameById(ui32 id) const {
    return FileConf.GetName(id);
}

TCmdView TSymbols::CmdNameById(ui32 id) const {
    return CommandConf.GetName(id);
}

TFileView TSymbols::FileNameByCacheId(TDepsCacheId cacheId) const {
    Y_ASSERT(IsFile(cacheId));
    return FileConf.GetName(ElemId(cacheId));
}

TCmdView TSymbols::CmdNameByCacheId(TDepsCacheId cacheId) const {
    Y_ASSERT(!IsFile(cacheId));
    return CommandConf.GetName(ElemId(cacheId));
}

ui32 TSymbols::AddName(EMakeNodeType type, TStringBuf name) {
    if (UseFileId(type)) {
        return FileConf.Add(name);
    } else {
        return CommandConf.Add(name);
    }
}

ui32 TSymbols::IdByName(EMakeNodeType type, TStringBuf name) const {
    if (UseFileId(type)) {
        return FileConf.GetId(name);
    } else {
        return CommandConf.GetId(name);
    }
}

void TSymbols::Save(TMultiBlobBuilder& builder) {
    TMultiBlobBuilder* fileConfBuilder = new TMultiBlobBuilder();
    FileConf.Save(*fileConfBuilder);
    builder.AddBlob(fileConfBuilder);

    TMultiBlobBuilder* commandConfBuilder = new TMultiBlobBuilder();
    CommandConf.Save(*commandConfBuilder);
    builder.AddBlob(commandConfBuilder);
}

void TSymbols::Load(TBlob& multi) {
    StaticStore = multi;
    TSubBlobs nameBlobs(multi);
    FileConf.Load(nameBlobs[0]);
    CommandConf.Load(nameBlobs[1]);
}

void TSymbols::Clear() {
    FileConf.Clear();
    CommandConf.Clear();
    StaticStore.Drop();
}

void TSymbols::Dump(IOutputStream& out) const{
    CommandConf.Dump(out);
    FileConf.Dump(out);
}
