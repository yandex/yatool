#pragma once

#include "base.h"

class TXsIncludeProcessor: public TIncludeProcessorBase {
public:
    TXsIncludeProcessor(TSymbols& symbols);
    ui32 Version() const override { return 1; }
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TInclDep>& includes) const;
    void ProcessOutputIncludes(TAddDepAdaptor& node,
                               TModuleWrapper& module,
                               TFileView incFileName,
                               const TVector<TString>& includes) const override;
};
