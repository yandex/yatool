#pragma once

#include <devtools/ymake/conf.h>

#include <devtools/ymake/symbols/file_store.h>

class TGraphChangesPredictor {
public:
    TGraphChangesPredictor(IChanges& changes)
    : Changes_(changes)
    , HasChanges_(false)
    {}

    bool HasChanges() const {
        return HasChanges_;
    }

    void AnalyzeChanges();

private:
    IChanges& Changes_;

    bool HasChanges_;
};
