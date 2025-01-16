#include "toolchain_latest_matched.h"

#include <devtools/ya/cpp/lib/class_registry.h>
#include <devtools/ya/cpp/lib/edl/json/from_json.h>

#include <util/string/join.h>

namespace NYa::NTool {
    struct TLatestMatchedInfo {
        Y_EDL_MEMBERS(
            ((long long) UpdateTime),
            ((long long) ResourceId)
        )
    };
}

namespace NYa::NTool {
    const TFsPath DISABLE_AUTO_UPDATE_FILE_NAME = ".disable.auto.update";

    TFsPath TLatestMatchedToolChainPathGetter::GetPath(const TFsPath& toolRoot, const NYaConfJson::TBottle& bottle, const NYaConfJson::TFormula& formula, const TCanonizedPlatform& forPlatform) const {
        if (!std::holds_alternative<NYaConfJson::TLatestMatchedFormula>(formula)) {
            return {};
        }
        const NYaConfJson::TLatestMatchedFormula& formulaValue = std::get<NYaConfJson::TLatestMatchedFormula>(formula);

        TFsPath toolChainPath = toolRoot / Join('-', bottle.Name, forPlatform.AsString());
        TFsPath infoPath = toolChainPath.GetPath() + ".info";
        long long updateTimeThreshold = time(nullptr) - formulaValue.LatestMatched.UpdateInterval;

        if (!infoPath.Exists()) {
            throw yexception() << "Info file doesn't exist: " << infoPath;
        }

        TLatestMatchedInfo info;
        try {
            NYa::NEdl::LoadJsonFromFile(infoPath.GetPath(), info);
        } catch (const yexception& e) {
            yexception() << "Can not read '" << infoPath << "': " << e.what();
        }

        TFsPath disableAutoUpdatePath = toolRoot / DISABLE_AUTO_UPDATE_FILE_NAME;
        if (info.UpdateTime < updateTimeThreshold && !disableAutoUpdatePath.Exists()) {
            throw yexception() << "It is time to update the tool";
        }

        return toolChainPath;
     }

    static NYa::TClassRegistrar<IToolChainPathGetter, TLatestMatchedToolChainPathGetter> registration{};
}
