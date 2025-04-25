#pragma once

#include <contrib/libs/jinja2cpp/include/jinja2cpp/template.h>

#include <string>

/// Auto generated internal attributes
namespace NInternalAttrs {
    inline static const std::string Name = "name";///< Name of target
    inline static const std::string Macro = "macro";///< Macro (semantic name) of target
    inline static const std::string MacroArgs = "macro_args";///< Macro arguments of target
    inline static const std::string IsTest = "is_test";///< This target is test
    inline static const std::string TestRelDir = "test_reldir";///< Relative to main target subdirectory of test
    inline static const std::string Subdirs = "subdirs";///< Subdirectories of directory
    inline static const std::string Curdir = "curdir";///< Current directory (relative to export root)
    inline static const std::string Target = "target";///< Main target of directory
    inline static const std::string ExtraTargets = "extra_targets";///< Extra targets of directory
    inline static const std::string HasTest = "has_test";///< Extra targets of directory has at least one test
    inline static const std::string Tools = "tools";///< Relative paths to tool binaries
    inline static const std::string ProjectName = "project_name";///< Project name of export
    inline static const std::string ArcadiaRoot = "arcadia_root";///< Full path to arcadia root
    inline static const std::string ExportRoot = "export_root";///< Full path to export root
    inline static const std::string ProjectRoot = "project_root";///< Full path to project root
    inline static const std::string DumpSems = "dump_sems";///< Dump of semantics
    inline static const std::string DumpAttrs = "dump_attrs";///< Dump of all attributes
    inline static const std::string Excludes = "excludes";///< Lists of excludes induced attributes
    inline static const std::string Testdep = "testdep";///< Dependency to test, if not empty, attr has path to library with test inside
    inline static const std::string RootAttrs = "root";///< Dump of root attributes (when dumping all attributes)
    inline static const std::string PlatformNames = "platform_names";///< List of all collected platform names
    inline static const std::string PlatformConditions = "platform_conditions";///< Map of platform name => platform condition (string)
    inline static const std::string PlatformAttrs = "platform_attrs";///< Map of platform name => platform attributes (map)

    /// DEPRECATED - remove after use snake case everywhere in templates
    inline static std::string CamelCase(const std::string& snakeCase) {
        auto endWord = snakeCase.find('_');
        if (endWord == std::string::npos) {
            return {}; // has no camel case name
        }
        std::string camelCase;
        auto len = snakeCase.size();
        size_t begWord = 0;
        size_t numWord = 0;
        while (endWord <= len) {
            if (endWord > begWord) {
                auto word = snakeCase.substr(begWord, endWord - begWord);
                auto first = word.at(0);
                if (first >= 'a' && first <= 'z' && numWord) {
                    word[0] = first - ('a' - 'A');
                }
                camelCase += word;
                ++numWord;
            }
            begWord = endWord + 1;
            if (begWord >= len) {
                break;
            }
            endWord = snakeCase.find('_', begWord);
            if (endWord == std::string::npos) {
                endWord = len;
            }
        }
        return camelCase;
    }

    inline static auto EmplaceAttr(jinja2::ValuesMap& map, const std::string snakeCaseName, jinja2::Value&& value) {
        return map.emplace(snakeCaseName, std::move(value));
    }
}
