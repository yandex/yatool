#include "autoincludes_conf.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/manager.h>

#include <devtools/ymake/module_state.h>
#include <library/cpp/json/json_reader.h>

#include <util/generic/algorithm.h>
#include <util/generic/yexception.h>
#include <util/stream/file.h>

TVector<TString> LoadAutoincludeRoots(const TVector<TFsPath>& configs) {
    TVector<TString> roots;
    for (const auto& config : configs) {
        YDIAG(Conf) << "Reading autoinclude conf file: " << config << Endl;
        try {
            TFileInput fileInput(config);
            const auto content = fileInput.ReadAll();
            NJson::TJsonValue json;
            ReadJsonTree(content, true, &json, true);
            if (!json.IsArray()) {
                YConfErr(Misconfiguration) << "Autoinclude conf root section should be an array." << Endl;
            } else {
                for (const auto& path : json.GetArray()) {
                    roots.push_back(NPath::ConstructYDir(path.GetString(), TStringBuf(), ConstrYDirDiag));
                }
            }
        } catch (const TFileError& e) {
            YConfErr(BadFile) << "Error while reading autoinclude config " << config << ": " << e.what() << Endl;
        } catch (const NJson::TJsonException& e) {
            YConfErr(BadFile) << "Autoinclude config " << config << " is invalid. Json syntax error: " << e.what() << Endl;
        }
    }
    SortUnique(roots);
    return roots;
}

TCompactTrieBuilder<char, TString> BuildAutoincludeTrie(const TVector<TString>& roots) {
    TCompactTrieBuilder<char, TString> trie;
    for (const auto& dir : roots) {
        trie.Add(dir + NPath::PATH_SEP_S, NPath::SmartJoin(dir, LINTERS_MAKE_INC));
    }
    return trie;
}

TString FindAutoinclude(const TCompactTrieBuilder<char, TString>& trie, TStringBuf dir) {
    size_t prefixLen;
    TString include;
    trie.FindLongestPrefix(TString::Join(dir, NPath::PATH_SEP_S), &prefixLen, &include);
    return include;
}
