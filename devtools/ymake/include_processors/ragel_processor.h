#pragma once

#include "base.h"
#include "include.h"

#include <devtools/ymake/include_parsers/ragel_parser.h>

class TRagelIncludeProcessor: public TIncludeProcessorBase {
public:
    ui32 Version() const override { return 1; }
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TRagelInclude>& includes) const;
    void ProcessOutputIncludes(TAddDepAdaptor& node,
                               TModuleWrapper& module,
                               TFileView incFileName,
                               const TVector<TString>& includes) const override;
protected:
    void PrepareIncludes(TVector<TResolveFile>& nativeResolved,
                         TVector<TResolveFile>& cppResolved,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TRagelInclude>& includes) const;
    void ScanIncludes(TVector<TInclude>& nativeIncludes,
                      TVector<TInclude>& cppIncludes,
                      const TVector<TRagelInclude>& includes) const;
};
