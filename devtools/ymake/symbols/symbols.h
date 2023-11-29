#pragma once

#include "name_data_store.h"
#include "file_store.h"
#include "cmd_store.h"

#include <devtools/ymake/compact_graph/dep_types.h>

#include <util/memory/blob.h>


class TSymbols {
private:
    TBlob StaticStore;

public:
    TCmdConf CommandConf;
    TFileConf FileConf;

public:
    TSymbols(const TRootsOptions& configuration, const TDebugOptions& debugOptions, TTimeStamps& timestamps)
            : FileConf(configuration, debugOptions, timestamps)
    {
    }

    TFileView FileNameById(ui32 id) const;
    TCmdView CmdNameById(ui32 id) const;

    TFileView FileNameByCacheId(TDepsCacheId cacheId) const;
    TCmdView CmdNameByCacheId(TDepsCacheId cacheId) const;

    ui32 IdByName(EMakeNodeType type, TStringBuf name) const;

    ui32 AddName(EMakeNodeType type, TStringBuf name);

    void Save(TMultiBlobBuilder& builder);

    void Load(TBlob& multi);

    void Clear();

    void Dump(IOutputStream& out);
};
