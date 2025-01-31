#include "ts_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/module_wrapper.h>

#include <util/folder/path.h>

namespace {
    const std::initializer_list<TStringBuf> AllowedImportExts = {
        "ts"sv,
        "d.ts"sv,
        "js"sv,
        "tsx"sv,
        "jsx"sv,
    };

    const std::initializer_list<std::pair<TStringBuf, TStringBuf>> AllowedReplacements = {
        {"js"sv, "ts"sv},
        {"js"sv, "tsx"sv},
    };

    const TStringBuf indexName = "index"sv;

    bool IsRelativeImport(TStringBuf import) {
        return import.StartsWith("./") || import.StartsWith("../") || import == "." || import == "..";
    }

    bool FindFile(TModuleWrapper& module, TStringBuf searchPath, TResolveFile& searchResultPath) {
        Y_ASSERT(searchResultPath.Empty());

        searchResultPath = module.ResolveSourcePath(searchPath, {}, TModuleResolver::Default, false);

        return !searchResultPath.Empty();
    }

    // If file exists with one of the allowed extensions
    bool FindFileWithAllowedExt(TModuleWrapper& module, TStringBuf searchPath, TResolveFile& searchResultPath, const std::initializer_list<TStringBuf>& extsToTry = AllowedImportExts) {
        Y_ASSERT(searchResultPath.Empty());

        for (const auto& ext : extsToTry) {
            if (FindFile(module, TString::Join(searchPath, ".", ext), searchResultPath)) {
                return true;
            }
        }

        return false;
    }

    // If file exists with one of the allowed extension replacements
    bool FindFileWithAllowedReplacement(TModuleWrapper& module, TStringBuf searchPath, TResolveFile& searchResultPath, const std::initializer_list<std::pair<TStringBuf, TStringBuf>>& replacements = AllowedReplacements) {
        Y_ASSERT(searchResultPath.Empty());

        const size_t dotPos = searchPath.rfind(".");
        if (dotPos == TString::npos || dotPos >= searchPath.size() - 1) {
            // No dot found or it is in the very end of the filename
            return false;
        }

        const size_t filenameStart = searchPath.rfind(NPath::PATH_SEP_S);
        if (filenameStart != TString::npos && dotPos < filenameStart) {
            // Last dot is not in the extention - it is in some directory name
            return false;
        }

        const TStringBuf ext = TStringBuf(searchPath, dotPos + 1, TStringBuf::npos);
        for (const auto& [fromExt, toExt] : replacements) {
            if (ext.equal(fromExt)) {
                const TStringBuf searchPathBase = TStringBuf(searchPath, 0, dotPos);
                if (FindFileWithAllowedExt(module, searchPathBase, searchResultPath, {toExt})) {
                    // File found with the ext replaced to one of the allowed (for esm import cases)
                    return true;
                }
            }
        }

        return false;
    }

    // TS Modules and CommonJS import resolution order:
    // - parent(importer) / moduleSpecifier,
    // - parent(importer) / moduleSpecifier . with one of the allowed extensions,
    // - parent(importer) / moduleSpecifier / "index" . one of the allowed extensions.
    // - parent(importer) / moduleSpecifier   with one of the allowed extension replacements
    TResolveFile ResolveRelativeImport(TModuleWrapper& module, TStringBuf prefix, TStringBuf import) {
        TResolveFile resolvedPath;

        const auto searchPath = NPath::Join(prefix, import);

        FindFile(module, searchPath, resolvedPath)                                                                   // Path as-is
            || FindFileWithAllowedExt(module, searchPath, resolvedPath)                                              // Path + one of the allowed extensions
            || FindFileWithAllowedExt(module, TString::Join(searchPath, NPath::PATH_SEP_S, indexName), resolvedPath) // Path + "/index." + one of the allowed extensions
            || FindFileWithAllowedReplacement(module, searchPath, resolvedPath);                                     // Path with allowed extension replacement (.js -> .ts)

        return resolvedPath;
    }
}

TVector<TString> TTsImportProcessor::GenerateOutputPaths(TStringBuf sourcePath, const TTsImportProcessor::TTsConfig& cfg) {
    Y_ASSERT(NPath::IsPrefixOf(cfg.RootDir, sourcePath));

    const auto srcRelPath = sourcePath.substr(cfg.RootDir.size() + 1);
    const auto outBase = TString::Join(BLD_DIR, NPath::PATH_SEP_S, NPath::CutType(cfg.OutDir), NPath::PATH_SEP_S, NPath::NoExtension(srcRelPath));
    const auto srcExt = NPath::Extension(sourcePath);
    const auto isTsOrJs = srcExt == "ts" || srcExt == "js";
    const auto isTsxOrJsx = srcExt == "tsx" || srcExt == "jsx";
    TVector<TString> outputs;
    // .js, .map, .d.ts, .d.ts.map
    outputs.reserve(4);

    if (isTsOrJs || isTsxOrJsx) {
        const auto outExt = (isTsxOrJsx && cfg.PreserveJsx) ? "jsx" : "js";
        outputs.push_back(TString::Join(outBase, '.', outExt));
        if (cfg.SourceMap) {
            outputs.push_back(TString::Join(outBase, '.', outExt, ".map"));
        }
        if (cfg.Declaration) {
            outputs.push_back(TString::Join(outBase, ".d.ts"));
        }
        if (cfg.DeclarationMap) {
            outputs.push_back(TString::Join(outBase, ".d.ts.map"));
        }
    } else {
        outputs.push_back(TString::Join(outBase, '.', srcExt));
    }

    return outputs;
}

TTsImportProcessor::TTsImportProcessor() {
    Rule.PassInducedIncludesThroughFiles = true;
}

void TTsImportProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                         TModuleWrapper& module,
                                         TFileView incFileName,
                                         const TVector<TString>& includes) const {
    if (module.GetModule().GetTag() == "TS") {
        // Process imports only for _TS_BASE_UNIT modules with js/ts sources.
        ProcessImports(node, module, incFileName, includes);
    }
}

void TTsImportProcessor::ProcessOutputIncludes(TAddDepAdaptor& node,
                                               TModuleWrapper& module,
                                               TFileView incFileName,
                                               const TVector<TString>& includes) const {
    ProcessIncludes(node, module, incFileName, includes);
}

void TTsImportProcessor::ProcessImports(TAddDepAdaptor& node,
                                        TModuleWrapper& module,
                                        TFileView incFileName,
                                        const TVector<TString>& includes) const {
    const auto importer = TString{incFileName.GetTargetStr()};
    const auto prefix = NPath::Parent(importer);
    auto ignoreNextImport = false;
    TVector<TResolveFile> resolvedImports;
    resolvedImports.reserve(includes.size());

    for (const auto& include : includes) {
        if (include == TTsImportParser::IGNORE_IMPORT) {
            ignoreNextImport = true;
            continue;
        }

        if (ignoreNextImport) {
            ignoreNextImport = false;
            continue;
        }

        if (include.StartsWith(TTsImportParser::PARSE_ERROR_PREFIX)) {
            YConfErr(UserErr) << "Failed to parse import in " << importer << ": " << include.substr(TTsImportParser::PARSE_ERROR_PREFIX.size());
            continue;
        }

        if (IsRelativeImport(include)) {
            const auto importPath = ResolveRelativeImport(module, prefix, include);
            if (importPath.Empty()) {
                YConfErr(UserErr) << "Failed to resolve import in " << importer << ": " << include;
                continue;
            }

            resolvedImports.push_back(importPath);
        }
    }

    AddIncludesToNode(node, resolvedImports, module);

    if (module.Get("TS_CONFIG_DEDUCE_OUT") != "no") {
        node.AddUniqueDep(EDT_Property, EMNT_Property, FormatProperty("Mod.TsDeduceOut", importer));
    }
}
