#include "sysincl_conf.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/manager.h>

#include <contrib/libs/yaml-cpp/include/yaml-cpp/yaml.h>
#include <library/cpp/digest/md5/md5.h>

#include <util/generic/iterator_range.h>
#include <util/generic/yexception.h>
#include <util/memory/blob.h>
#include <util/stream/file.h>
#include <util/string/builder.h>

namespace {
    class TSysinclConfig {
    public:
        /// @brief appends config from provided content to resolver
        void LoadConfigContent(const TString& content, TStringBuf path = "");

        TSysinclRules&& Rules() {
            return std::move(Rules_);
        }

    private:
        void ParseIncludeSection(const YAML::Node& root, TStringBuf context);
        void ParseIncludeList(const YAML::Node& root,
                              bool caseSensitive,
                              TStringBuf srcFilter,
                              TStringBuf context);
        void Append(TStringBuf include,
                    bool caseSensitive,
                    TStringBuf srcFilter,
                    TStringBuf target);

        TSysinclRules Rules_;
    };

    constexpr char CASE_SENSITIVE_KEY[] = "case_sensitive";
    constexpr char SRC_FILTER_KEY[] = "source_filter";
    constexpr char INCLUDES_KEY[] = "includes";

    bool IsIncludeSection(const YAML::Node& node) {
        return node.IsMap() && node[INCLUDES_KEY] && node[INCLUDES_KEY].IsSequence();
    }

    TString MarkStr(const YAML::Mark& mark) {
        TStringBuilder sb;
        return sb << "line: " << mark.line << " column: " << mark.column;
    }

    TString MarkStr(const YAML::Node& node) {
        return MarkStr(node.Mark());
    }
}

TSysinclRules LoadSystemIncludes(const TVector<TFsPath>& configs,
                                 MD5& confData) {
    TSysinclConfig config;
    for (const TFsPath& path : configs) {
        YDIAG(Conf) << "Reading sysincl file: " << path << Endl;
        try {
            TFileInput fileInput(path);
            TString content = fileInput.ReadAll();
            confData.Update(content.data(), content.size());
            config.LoadConfigContent(content, path.GetPath());
        } catch (const TFileError& e) {
            YConfErr(BadFile) << "Error while reading sysincl config " << path << ": " << e.what() << Endl;
        }
    }
    return config.Rules();
}

TSysinclRules LoadSystemIncludes(const TString& content) {
    TSysinclConfig config;
    config.LoadConfigContent(content, "");
    return config.Rules();
}

void TSysinclConfig::LoadConfigContent(const TString& content, TStringBuf path) {
    try {
        YAML::Node root = YAML::Load(content.c_str());
        if (IsIncludeSection(root)) {
            ParseIncludeSection(root, path);
            return;
        }
        if (root.IsSequence() && IsIncludeSection(root[0])) {
            for (const YAML::Node& node : root) {
                ParseIncludeSection(node, path);
            }
            return;
        }
        ParseIncludeList(root, true, "", path);
    } catch (const YAML::Exception& e) {
        YConfErr(BadFile) << "Sysincl config " << path << " is invalid."
                           << " Yaml syntax error at " << MarkStr(e.mark) << Endl;
    }
}

void TSysinclConfig::ParseIncludeSection(const YAML::Node& root, TStringBuf context) {
    try {
        YDIAG(Conf) << "Parsing sysincl section." << Endl;
        if (!IsIncludeSection(root)) {
            YConfErr(Misconfiguration)
                << "Sysincl section at " << MarkStr(root) << " in " << context
                << " is invalid." << Endl;
            return;
        }
        const auto fres = FindIf(
            root.begin(),
            root.end(),
            [](const auto& kv) { return !EqualToOneOf(kv.first.template as<std::string>(), CASE_SENSITIVE_KEY, SRC_FILTER_KEY, INCLUDES_KEY); });
        if (fres != root.end()) {
            YConfErr(Misconfiguration)
                << "Sysincl include section with unsupported key '" << fres->first.as<std::string>() << "': " << MarkStr(root) << " in " << context
                << " is invalid." << Endl;
        }
        TStringBuf srcFilter;
        const YAML::Node& filterNode = root[SRC_FILTER_KEY];
        if (filterNode) {
            if (filterNode.IsScalar()) {
                srcFilter = filterNode.Scalar();
            } else {
                YConfErr(Misconfiguration)
                    << "Sysincl source_filter " << MarkStr(filterNode) << " in " << context
                    << " is invalid. Must be regex as yaml scalar." << Endl;
                return;
            }
        }
        const YAML::Node& caseNode = root[CASE_SENSITIVE_KEY];
        bool caseSensitive = caseNode && caseNode.IsScalar() ? caseNode.as<bool>() : true;
        ParseIncludeList(root[INCLUDES_KEY], caseSensitive, srcFilter,
                         TString::Join("'", srcFilter, "' in ", context));
    } catch (const yexception& e) {
        YConfErr(Misconfiguration)
            << "Error while parsing sysincl section at " << MarkStr(root) << " in " << context
            << ": " << e.what() << Endl;
    }
}

void TSysinclConfig::ParseIncludeList(const YAML::Node& root,
                                      bool caseSensitive,
                                      TStringBuf srcFilter,
                                      TStringBuf context) {
    YDIAG(Conf) << "Parsing sysincl include list." << Endl;
    if (!root.IsSequence()) {
        YConfErr(BadFile) << "Sysincl include list in " << context
                           << " is invalid. Must be yaml sequence." << Endl;
        return;
    }
    for (const YAML::Node& entry : root) {
        if (entry.IsScalar()) {
            Append(entry.Scalar(), caseSensitive, srcFilter, "");
        } else if (entry.IsMap()) {
            if (entry.size() > 1) {
                YConfWarn(Misconfiguration)
                    << "Sysincl item at " << MarkStr(entry) << " in " << context
                    << " has multiple keys that will be processed as separate entries" << Endl;
            }
            for (const auto& mapEntry : entry) {
                YAML::Node keyEntry = mapEntry.first;
                YAML::Node valEntry = mapEntry.second;
                if (!keyEntry.IsScalar()) {
                    YConfErr(Misconfiguration)
                        << "Sysincl item at " << MarkStr(keyEntry) << " in " << context
                        << " must have scalar as key." << Endl;
                    continue;
                }
                TStringBuf key = keyEntry.Scalar();
                if (valEntry.IsNull()) {
                    YConfWarn(Misconfiguration)
                        << "Sysincl item " << keyEntry.Scalar() << " in " << context
                        << " has empty value." << Endl;
                    Append(key, caseSensitive, srcFilter, "");
                } else if (valEntry.IsScalar()) {
                    Append(key, caseSensitive, srcFilter, valEntry.Scalar());
                } else if (valEntry.IsSequence()) {
                    for (const YAML::Node& target : valEntry) {
                        if (!target.IsScalar()) {
                            YConfErr(Misconfiguration)
                                << "Sysincl list target for " << key << " in " << context
                                << " has invalid non-scalar target at " << MarkStr(target) << Endl;
                            continue;
                        }
                        Append(key, caseSensitive, srcFilter, target.Scalar());
                    }
                }
            }
        } else {
            YConfErr(Misconfiguration)
                << "Sysincl item at " << MarkStr(entry) << " in " << context
                << " is invalid." << Endl;
        }
    }
}

void TSysinclConfig::Append(TStringBuf include,
                            bool caseSensitive,
                            TStringBuf srcFilter,
                            TStringBuf target) {
    Y_ASSERT(!include.empty());
    YDIAG(Conf) << "Sysincl append incl: " << include << " filter: " << srcFilter << " tgt: " << target << Endl;

    //We need ensure that filter regex compiles before we store current rule
    TString filter;
    if (srcFilter) {
        decltype(Rules_.Filters)::insert_ctx ctx = nullptr;
        auto filterIter = Rules_.Filters.find(srcFilter, ctx);
        if (filterIter == Rules_.Filters.end()) {
            filter = srcFilter;
            Rules_.Filters.emplace_direct(ctx, std::piecewise_construct,
                                          std::forward_as_tuple(filter),
                                          std::forward_as_tuple(filter));
        } else {
            filter = filterIter->first;
        }
    }

    TString lower(include);
    lower.to_lower();
    const auto range = MakeIteratorRange(Rules_.Headers.equal_range(lower));
    auto same = [include, caseSensitive, filter](auto& p) {
        const auto& rule = p.second;
        if (caseSensitive != rule.CaseSensitive || filter != rule.SrcFilter) {
            return false;
        }
        if (caseSensitive && include != rule.Include) {
            return false;
        }
        return true;
    };
    auto ruleIter = FindIf(range, same);
    if (ruleIter == range.end()) {
        TSysinclRules::TRule rule;
        rule.Include = include;
        rule.CaseSensitive = caseSensitive;
        rule.SrcFilter = filter;
        ruleIter = Rules_.Headers.emplace(lower, std::move(rule));
    }

    TSysinclRules::TRule& rule = ruleIter->second;
    if (target) {
        if (NPath::IsTypedPath(target)) {
            rule.Targets.emplace_back(target);
        } else {
            rule.Targets.emplace_back(NPath::Join("${ARCADIA_ROOT}", target));
        }
    }
}
