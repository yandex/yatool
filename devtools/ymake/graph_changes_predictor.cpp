#include "graph_changes_predictor.h"

#include <devtools/ymake/common/npath.h>

void TGraphChangesPredictor::AnalyzeChanges() {

    auto&& markChanged = [this](const IChanges::TChange& change) {
        if (HasChanges_) {
            return;
        }

        auto type = change.Type;

        if (type == EChangeType::Remove) {
            HasChanges_ = true;
            YDebug() << "Graph has structural changes because " << change.Name << " file was removed" << Endl;
            return;
        }

        if (type == EChangeType::Create) {
            HasChanges_ = true;
            YDebug() << "Graph has structural changes because " << change.Name << " file was created" << Endl;
            return;
        }

        TString fullPath = NPath::ConstructPath(change.Name, NPath::ERoot::Source);
        auto fileView = FileConf_.GetStoredName(fullPath);
        auto fileData = FileConf_.GetFileData(fileView);
        if (fileData.IsMakeFile) {
            HasChanges_ = true;
            YDebug() << "Graph has structural changes because " << change.Name << " file has changes" << Endl;
            return;
        }

        const auto* parser = IncParserManager_.GetParserFor(fileView);
        if (parser == nullptr) {
            // Here we rely on the fact that a file is parsed only once and in one context and,
            // moreover, there can't be more than one entry in the parsers' cache
            if (auto parserType = IncParserManager_.Cache.GetParserType(fileView.GetTargetId());
                parserType !=  EIncludesParserType::BAD_PARSER)
            {
                parser = IncParserManager_.GetParserByType(parserType);
            }
        }
        if (parser != nullptr && parser->GetParserId().GetType() != EIncludesParserType::EmptyParser)
        {
            auto fileContent = FileConf_.GetFileByName(fileView);
            if (IncParserManager_.HasIncludeChanges(*fileContent, parser)) {
                HasChanges_ = true;
                YDebug() << "Graph has structural changes because " << change.Name << " file has includes changes" << Endl;
                return;
            }
        }
    };

    Changes_.Walk(markChanged);
}
