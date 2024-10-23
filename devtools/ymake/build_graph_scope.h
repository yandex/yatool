#pragma once

#include <devtools/ymake/ymake.h>

class TBuildGraphScope {
    TUpdIter UpdIter_;
    TGeneralParser Parser_;
public:
    explicit TBuildGraphScope(TYMake& yMake)
        : UpdIter_(yMake)
        , Parser_(yMake)
    {
        FORCE_TRACE(U, NEvent::TStageStarted("Build graph"));
        yMake.UpdIter = &UpdIter_;
        yMake.Parser = &Parser_;
    }
    ~TBuildGraphScope() {
        UpdIter_.YMake.UpdIter = nullptr;
        UpdIter_.YMake.Parser = nullptr;
        FORCE_TRACE(U, NEvent::TStageFinished("Build graph"));
    }
};
