#include "ts_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/module_wrapper.h>

#include <util/folder/path.h>

namespace {
    std::initializer_list<TString> FileImportExts = {
        "",
        ".ts",
        ".js",
        ".tsx",
        ".jsx",
    };

    std::initializer_list<TString> DirImportExts = {
        ".ts",
        ".js",
        ".tsx",
        ".jsx",
    };

    bool IsRelativeImport(const TString& import) {
        return import.StartsWith("./") || import.StartsWith("../") || import == "." || import == "..";
    }

    // TS Modules and CommonJS import resolution order:
    // - parent(importer) / moduleSpecifier,
    // - parent(importer) / moduleSpecifier . with one of the allowed extensions,
    // - parent(importer) / moduleSpecifier / "index" . one of the allowed extensions.
    TString ResolveRelativeImport(TModuleWrapper& module, const TStringBuf& prefix, const TString& import) {
        const auto searchPath = NPath::Join(prefix, import);

        for (const auto& ext : FileImportExts) {
            auto resolveFile = module.ResolveSourcePath(TString::Join(searchPath, ext), {}, TModuleResolver::Default, false);
            if (!resolveFile.Empty()) {
                return module.GetStr(resolveFile);
            }
        }

        for (const auto& ext : DirImportExts) {
            auto resolveFile = module.ResolveSourcePath(TString::Join(searchPath, NPath::PATH_SEP_S, "index", ext), {}, TModuleResolver::Default, false);
            if (!resolveFile.Empty()) {
                return module.GetStr(resolveFile);
            }
        }

        return "";
    }
}

TVector<TString> TTsImportProcessor::GenerateOutputPaths(const TString& sourcePath, const TTsImportProcessor::TTsConfig& cfg) {
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
    if (module.GetModule().GetTag() != "TS") {
        // Do not process imports of non-_TS_BASE_UNIT modules with js/ts sources.
        return;
    }

    ProcessImports(node, module, incFileName, includes);
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
    TVector<TString> resolvedImports;
    resolvedImports.reserve(includes.size());

    for (const auto& include : includes) {
        if (include == TTsImportParser::IGNORE_IMPORT) {
            ignoreNextImport = true;
            continue;
        }

        if (include.StartsWith(TTsImportParser::PARSE_ERROR_PREFIX)) {
            if (ignoreNextImport) {
                ignoreNextImport = false;
            } else {
                YConfErr(UserErr) << "Failed to parse import in " << importer << ": " << include.substr(TTsImportParser::PARSE_ERROR_PREFIX.size());
            }

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

    if (!resolvedImports.empty()) {
        AddIncludesToNode(node, resolvedImports);
    }

    if (module.Get("TS_CONFIG_DEDUCE_OUT") == "no") {
        return;
    }

    node.AddUniqueDep(EDT_Property, EMNT_Property, FormatProperty("Mod.TsDeduceOut", importer));
}
