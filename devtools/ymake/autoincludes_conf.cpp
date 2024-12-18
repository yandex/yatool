#include "autoincludes_conf.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/manager.h>

#include <devtools/ymake/module_state.h>
#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/json/json_reader.h>

#include <util/generic/yexception.h>
#include <util/stream/file.h>

TCompactTrieBuilder<char, TString> LoadAutoincludes(const TVector<TFsPath>& configs, MD5& confData) {
    TCompactTrieBuilder<char, TString> AutoincludePathsTrie;
    for (const auto& config : configs) {
        YDIAG(Conf) << "Reading autoinclude conf file: " << config << Endl;
        try {
            TFileInput fileInput(config);
            const auto content = fileInput.ReadAll();
            confData.Update(content.data(), content.size());
            NJson::TJsonValue json;
            ReadJsonTree(content, true, &json, true);
            if (!json.IsArray()) {
                YConfErr(Misconfiguration) << "Autoinclude conf root section should be an array." << Endl;
            } else {
                for (const auto& path : json.GetArray()) {
                    auto dir = NPath::ConstructYDir(path.GetString(), TStringBuf(), ConstrYDirDiag);
                    auto LintersMake = NPath::SmartJoin(dir, LINTERS_MAKE_INC);
                    AutoincludePathsTrie.Add(dir + NPath::PATH_SEP_S, LintersMake);
                }
            }
        } catch (const TFileError& e) {
            YConfErr(BadFile) << "Error while reading autoinclude config " << config << ": " << e.what() << Endl;
        } catch (const NJson::TJsonException& e) {
            YConfErr(BadFile) << "Autoinclude config " << config << " is invalid. Json syntax error: " << e.what() << Endl;
        }
    }
    return AutoincludePathsTrie;
}
