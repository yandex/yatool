#include "go_processor.h"
#include "cpp_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/dirs.h>
#include <devtools/ymake/macro_processor.h>
#include <devtools/ymake/module_wrapper.h>
#include <devtools/ymake/module_dir.h>
#include <devtools/ymake/ymake.h>

#include <util/string/split.h>
#include <util/system/yassert.h>

TGoImportProcessor::TGoImportProcessor(const TEvaluatorBase& evaluator, TSymbols& symbols)
    : Symbols(symbols)
{
    StdLibPrefix = evaluator.EvalVarValue(TStringBuf("GO_STD_LIB_PREFIX"));
    StdCmdPrefix = evaluator.EvalVarValue(TStringBuf("GO_STD_CMD_PREFIX"));
    ArcadiaProjectPrefix = evaluator.EvalVarValue(TStringBuf("GO_ARCADIA_PROJECT_PREFIX"));
    ContribProjectPrefix = evaluator.EvalVarValue(TStringBuf("GO_CONTRIB_PROJECT_PREFIX"));
    TString skipImports = evaluator.EvalVarValue(TStringBuf("GO_SKIP_IMPORTS"));
    TVector<TStringBuf> imports;
    Split(skipImports, " ", imports);
    SkipImports.insert(imports.begin(), imports.end());

    Rule.Actions.clear();
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "go"}, TIndDepsRule::EAction::Use));
    Rule.Actions.push_back(std::make_pair(TPropertyType{symbols, EVI_InducedDeps, "h+cpp"}, TIndDepsRule::EAction::Pass));
}

void TGoImportProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                         TModuleWrapper& module,
                                         TFileView incFileName,
                                         const TVector<TParsedFile>& parsedIncludes,
                                         bool fromOutputIncludes) const {
    Y_ASSERT(incFileName.GetType() == NPath::Source || fromOutputIncludes);

    if (parsedIncludes.empty()) {
        // Nothing to do - no imports found
        node.AddDirsToProps(TVector<ui32>(), TStringBuf("Mod.PeerDirs"));
        return;
    }

    const auto& selfDir = module.UnitPath();
    TVector<TString> storage; // Do not move down the storage!
    TDirs peerDirs;
    TVector<TInclude> includes;
    TString resolved;
    for (const auto& parsedFile : parsedIncludes) {
        const auto& include = parsedFile.ParsedFile;
        switch (parsedFile.Kind) {
            case TParsedFile::EKind::Import:
                {
                    ProcessImport(include, resolved, incFileName.CutType().StartsWith(StdLibPrefix), incFileName.CutType().StartsWith(StdCmdPrefix));
                    if (resolved.empty()) {
                        break;
                    }
                    auto peerdir = NPath::ConstructPath(resolved, NPath::ERoot::Source);
                    if (selfDir == peerdir) {
                        break;
                    }
                    storage.emplace_back(std::move(peerdir));
                    ui32 dirId = Symbols.AddName(EMNT_Directory, storage.back());
                    peerDirs.Push(Symbols.FileNameById(dirId));
                }
                break;
            case TParsedFile::EKind::Include:
                {
                    Y_ASSERT(!fromOutputIncludes); // Currently all OUTPUT_INCLUDES are treated as imports
                    TInclude inc;
                    if (TryPrepareCppInclude(include, inc)) {
                        includes.emplace_back(std::move(inc));
                    }
                }
                break;
            default:
                Y_ASSERT(false);
        }
    }

    // Add PEERDIR-s to immediate dependencies
    node.AddDirsToProps(peerDirs, TStringBuf("Mod.PeerDirs"));

    // Add parsed includes for CGO
    TVector<TResolveFile> resolvedIncludes(Reserve(includes.size()));
    module.ResolveIncludes(incFileName, includes, resolvedIncludes, LanguageId);
    AddIncludesToNode(node, resolvedIncludes, module);

    resolvedIncludes.clear();
    module.ResolveAsUnset(includes, resolvedIncludes);
    node.AddParsedIncls("h+cpp", resolvedIncludes);
    node.AddParsedIncls("go", resolvedIncludes);
}

void TGoImportProcessor::ProcessImport(const TString& import, TString& resolved, bool is_std, bool is_std_cmd) const {
    Y_ASSERT(!import.empty());
    resolved.clear();
    auto slashPos = import.find('/');
    auto dotPos = TString::npos;
    if (TString::npos != slashPos) {
        dotPos = import.rfind('.', slashPos);
    } else if (SkipImports.contains(import)) {
        return;
    } else {
        dotPos = import.find('.');
    }
    if (TString::npos == dotPos) {
        // Standard library
        resolved += StdLibPrefix;
        if (TString::npos != slashPos && import.StartsWith("golang_org/")) {
            resolved += "vendor/";
        }
        resolved += import;
    } else if (import.StartsWith(ArcadiaProjectPrefix)) {
        resolved = import.substr(ArcadiaProjectPrefix.size());
    } else {
        if (is_std_cmd) {
            // imports for toolchain should be resolved to its own vendor
            resolved += StdCmdPrefix;
            resolved += "vendor/";
        } else if (is_std) {
            // imports for standard library should be resolved to its own vendor
            resolved += StdLibPrefix;
            resolved += "vendor/";
        } else {
            resolved += ContribProjectPrefix;
        }
        resolved += import;
    }
}

void TGoImportProcessor::ProcessOutputIncludes(TAddDepAdaptor& node,
                                               TModuleWrapper& module,
                                               TFileView incFileName,
                                               const TVector<TString>& includes) const {
    TVector<TParsedFile> typedIncludes(Reserve(includes.size()));
    for (const auto& incl : includes) {
        //Maybe it is overkill, but it's safer to produce more dependencies
        typedIncludes.emplace_back(incl, TParsedFile::EKind::Import);
// FIXME!!! This cannot work correctly: TryPrepareCppInclude() called on this
//          expects "name" or <name> which is naturally not the case for OUTPUT_INCLUDES
//          We should either implement same logic as in cython_processor or rely on IDUCED_DEPS
//          parameter which allows more granular control over dependencies
//        typedIncludes.emplace_back(incl, TParsedFile::EKind::Include);
    }
    ProcessIncludes(node, module, incFileName, typedIncludes, true);
}
