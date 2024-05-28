#include "graph_changes_predictor.h"

void TGraphChangesPredictor::AnalyzeChanges() {

    auto&& markChanged = [this](const IChanges::TChange& change) {
        auto type = change.Type;

        if (type == EChangeType::Remove) {
            HasChanges_ = true;
            return;
        }

        if (type == EChangeType::Create) {
            HasChanges_ = true;
            return;
        }
    };

    Changes_.Walk(markChanged);
}
