#pragma once

#include "base.h"

class TFlatcIncludeProcessorBase: public TStringIncludeProcessor {
public:
    ui32 Version() const override { return 2; }
    TFlatcIncludeProcessorBase(TStringBuf inducedFrom,
                               TStringBuf inducedTo);
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TString>& includes) const override;

private:
    TString MakeInduced(const TString& include) const;

    const TStringBuf InducedFrom;
    const TStringBuf InducedTo;
};

class TFlatcIncludeProcessor: public TFlatcIncludeProcessorBase {
public:
    TFlatcIncludeProcessor()
        : TFlatcIncludeProcessorBase(".fbs", ".fbs.h")
    {
    }
};

class TFlatcIncludeProcessor64: public TFlatcIncludeProcessorBase {
public:
    TFlatcIncludeProcessor64()
        : TFlatcIncludeProcessorBase(".fbs64", ".fbs64.h")
    {
    }
};
