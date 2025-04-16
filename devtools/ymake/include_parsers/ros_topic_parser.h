#pragma once

#include "ros_parser.h"

class TRosTopicIncludeParser {
public:
    void Parse(IContentHolder& file, TVector<TRosDep>& parsedIncludes) const;
};
