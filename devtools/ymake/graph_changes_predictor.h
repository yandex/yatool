#pragma once

#include <devtools/ymake/conf.h>
#include <devtools/ymake/parser_manager.h>

#include <devtools/ymake/symbols/file_store.h>

class TGraphChangesPredictor {
public:
    TGraphChangesPredictor(const TIncParserManager& parserManager, TFileConf& fileConf, IChanges& changes)
    : IncParserManager_(parserManager)
    , FileConf_(fileConf)
    , Changes_(changes)
    , HasChanges_(false)
    {}

    bool HasChanges() const {
        return HasChanges_;
    }

    void AnalyzeChanges();

private:
    const TIncParserManager& IncParserManager_;
    TFileConf& FileConf_;
    IChanges& Changes_;

    bool HasChanges_;
};
