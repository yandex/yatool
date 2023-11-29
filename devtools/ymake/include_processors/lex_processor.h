#pragma once

#include "base.h"

class TLexIncludeProcessor: public TStringIncludeProcessor {
public:
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TString>& includes) const override;
};
