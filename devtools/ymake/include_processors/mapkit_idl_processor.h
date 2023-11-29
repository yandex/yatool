#pragma once

#include "base.h"

class TMapkitIdlIncludeProcessor: public TNoInducedIncludeProcessor {
public:
    explicit TMapkitIdlIncludeProcessor(TSymbols& symbols);
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TString>& includes) const override;
private:
    TLangId LanguageProto;
};
