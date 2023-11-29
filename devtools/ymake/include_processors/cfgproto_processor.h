#pragma once

#include "proto_processor.h"

class TCfgprotoIncludeProcessor: public TProtoIncludeProcessor {
public:
    explicit TCfgprotoIncludeProcessor(TSymbols& symbols);
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TInclDep>& includes) const;

private:
    void ProcessHIncludes(TVector<TResolveFile>& hResolved,
                          TVector<TString>& nativeIncludes,
                          TModuleWrapper& module,
                          TFileView incFileName,
                          const TVector<TInclDep>& parsedIncludes) const;

    void ProcessIncludes(TAddDepAdaptor&, TModuleWrapper&, TFileView, const TVector<TString>&) const override {
        Y_ASSERT(false); // This is not expected to be called
    }
};
