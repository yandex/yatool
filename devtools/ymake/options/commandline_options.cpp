#include "commandline_options.h"

void TCommandLineOptions::AddOptions(NLastGetopt::TOpts& opts) {
    opts.AddLongOption('J', "dump-build-plan", "dump build plan").StoreResult(&WriteJSON);
    opts.AddLongOption('X', "find-path-from", "find (any) path from this target").EmplaceTo(&FindPathFrom);
    opts.AddLongOption('Z', "find-path-to", "find (any) path to this target").EmplaceTo(&FindPathTo);
    opts.AddLongOption("managed-dep-tree", "print dependency tree with DEPENDENCY_MANAGEMENT explanation").EmplaceTo(&ManagedDepTreeRoots);
    opts.AddLongOption("managed-deps", "print managed dependencies for the given target").EmplaceTo(&DumpDMRoots);
    opts.AddLongOption('m', "write-meta-data", "metadata export file").StoreResult(&WriteMetaData);
    opts.AddLongOption('z', "cache-path", "load cache from path and do not update graph").StoreResult(&CachePath);
    opts.AddLongOption('p', "patch-path", "use list of changes from patch (.zipatch) or from arc changelist (.cl)").StoreResult(&PatchPath);

    opts.AddLongOption('a', "add-tests", "add tests to graph").SetFlag(&Test).NoArgument();
    opts.AddLongOption('k', "keep-going", "make as much as possible").SetFlag(&KeepGoing).NoArgument();
    opts.AddLongOption("keep-on", "deprecated alias to --keep-going").SetFlag(&KeepOn).NoArgument();
    opts.AddLongOption('v', "verbose", "display os commands before executing").SetFlag(&VerboseMake).NoArgument();
    opts.AddLongOption('d', "depends-like-recurse", "handle DEPENDS like RECURSEs").SetFlag(&DependsLikeRecurse).NoArgument();
    opts.AddLongOption('g', "quiet", "disable human-readable output").SetFlag(&DisableHumanReadableOutput).NoArgument();
    opts.AddLongOption('G', "add-inputs-map", "dump inputs map into build plan").SetFlag(&DumpInputsMapInJSON).NoArgument();
    opts.AddLongOption('N', "add-inputs", "dump inputs for build plan nodes").SetFlag(&DumpInputsInJSON).NoArgument();
    opts.AddLongOption('e', "check-data-paths", "check paths in DATA section and emit configure error if they are absent on current filesystem").SetFlag(&CheckDataPaths).NoArgument();
    opts.AddLongOption("apply-zipatch", "read file content from zipatch").SetFlag(&ReadFileContentFromZipatch).NoArgument();

    opts.AddLongOption('A', "test-dart", "test_dart export file").StoreResult(&WriteTestDart);
    opts.AddLongOption('H', "java-dart", "java_dart export file").StoreResult(&WriteJavaDart);
    opts.AddLongOption('o', "makefiles-dart", "makefiles.dart export file").StoreResult(&WriteMakeFilesDart);
    opts.AddLongOption('Y', "yndex", "write json yndex to file and exit").StoreResult(&WriteYdx);

    opts.AddLongOption('P', "ide-project-path", "write an IDE project").StoreResult(&WriteIDEProj);
    opts.AddLongOption('U', "ide-project-name", "IDE project custom name").StoreResult(&IDEProjName);
    opts.AddLongOption('V', "ide-project-dir", "IDE project output path").StoreResult(&IDEProjDir);
    opts.AddLongOption("modules-info-file", "dump information for modules into file specified").StoreResult(&ModulesInfoFile);
    opts.AddLongOption("modules-info-filter", "dump only information for modules matching regexp (for h)").StoreResult(&ModulesInfoFilter);
    opts.AddLongOption("json-compression-codec", "compress json using specified codec (uber compressor format)").StoreResult(&JsonCompressionCodec);
}

void TCommandLineOptions::PostProcess(const TVector<TString>& /* freeArgs */) {
    if (WriteTestDart.empty()) {
        CheckDataPaths = false;
    }
    if (KeepGoing || KeepOn) {
        KeepGoing = KeepOn = true;
    }

    if (DumpInputsInJSON) {
        StoreInputsInJsonCache = false;
    }
}
