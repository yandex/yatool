#pragma once

#include "base.h"

#include <devtools/ymake/include_parsers/ros_parser.h>

class TRosIncludeProcessor: public TIncludeProcessorBase {
public:
    void ProcessIncludes(
        TAddDepAdaptor& node,
        TModuleWrapper& module,
        TFileView incFileName,
        const TVector<TRosDep>& includes) const;

    void ProcessOutputIncludes(
        TAddDepAdaptor& node,
        TModuleWrapper& module,
        TFileView incFileName,
        const TVector<TString>& includes) const override;
};
