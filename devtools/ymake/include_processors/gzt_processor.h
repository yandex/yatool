#pragma once

#include "proto_processor.h"

class TGztIncludeProcessor: public TProtoIncludeProcessor {
public:
    explicit TGztIncludeProcessor(TSymbols& symbols);
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TString>& includes) const override;

private:
    void GenerateProtoIncludes(TVector<TResolveFile>& protoIncludes,
                               const TVector<TResolveFile>& nativeIncludes,
                               TModuleWrapper& module) const;
};
