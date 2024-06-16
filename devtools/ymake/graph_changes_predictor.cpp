#include "graph_changes_predictor.h"

#include <devtools/ymake/common/npath.h>

void TGraphChangesPredictor::AnalyzeChanges() {

    auto&& markChanged = [this](const IChanges::TChange& change) {
        YDebug() << "Check Changes: " << change.Name << Endl;
        auto type = change.Type;

        if (type == EChangeType::Remove) {
            HasChanges_ = true;
            return;
        }

        if (type == EChangeType::Create) {
            HasChanges_ = true;
            return;
        }

        TString fullPath = NPath::ConstructPath(change.Name, NPath::ERoot::Source);
        auto fileView = FileConf.GetStoredName(fullPath);
        auto fileData = FileConf.GetFileData(fileView);
        if (fileData.IsMakeFile) {
            HasChanges_ = true;
            return;
        }

    };

    Changes_.Walk(markChanged);
}
