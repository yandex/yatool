#pragma once

#include "base.h"

class TNlgIncludeProcessor: public TIncludeProcessorBase {
public:
    TNlgIncludeProcessor();
    ui32 Version() const override { return 2 + CommonVersion; }
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& moduleWrapper,
                         TFileView incFileName,
                         const TVector<TInclDep>& inclDeps) const;
    void ProcessOutputIncludes(TAddDepAdaptor& node,
                               TModuleWrapper& moduleWrapper,
                               TFileView incFileName,
                               const TVector<TString>& includes) const override;
};
