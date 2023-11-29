#pragma once

#include "base.h"

class TYDLIncludeProcessor: public TStringIncludeProcessor {
public:
    ui32 Version() const override { return 1; }
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TString>& includes) const override;
};

