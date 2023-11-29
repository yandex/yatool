#pragma once

#include "base.h"

class TFortranIncludeProcessor: public TStringIncludeProcessor {
public:
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TString>& includes) const override;
};
