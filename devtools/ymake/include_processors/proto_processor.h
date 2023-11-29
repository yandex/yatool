#pragma once

#include "base.h"

#include <devtools/ymake/common/npath.h>

class TProtoIncludeProcessor: public TStringIncludeProcessor {
public:
    explicit TProtoIncludeProcessor(TSymbols& symbols);
    ui32 Version() const override { return 1; }
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TString>& includes) const override;

protected:
    void PrepareIncludes(TVector<TResolveFile>& nativeResolved,
                         TVector<TResolveFile>& hResolved,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TString>& parsedIncludes) const;
    void ParseProtoIncludes(TAddDepAdaptor& node,
                            TModuleWrapper& module,
                            TFileView incFileName,
                            TVector<TResolveFile>& nativeIncludes,
                            TVector<TResolveFile>& hIncludes,
                            const TVector<TString>& parsedIncludes) const;
};
