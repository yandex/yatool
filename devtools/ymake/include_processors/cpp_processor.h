#pragma once

#include "base.h"
#include "include.h"

class TCppIncludeProcessor: public TIncludeProcessorBase {
public:
    ui32 Version() const override { return 2 + CommonVersion; }
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TString>& includes) const;
    void ProcessOutputIncludes(TAddDepAdaptor& node,
                               TModuleWrapper& module,
                               TFileView incFileName,
                               const TVector<TString>& includes) const override;
};

class TCLikeIncludeProcessor: public TCppIncludeProcessor {
public:
    explicit TCLikeIncludeProcessor(TSymbols& symbols);
};

class TCHeaderIncludeProcessor: public TCppIncludeProcessor {
public:
    explicit TCHeaderIncludeProcessor(TSymbols& symbols);
};

bool TryPrepareCppInclude(const TString& include, TInclude& result);
