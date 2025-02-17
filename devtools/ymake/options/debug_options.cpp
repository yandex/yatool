#include "debug_options.h"

#include <util/generic/algorithm.h>
#include <util/generic/strbuf.h>
#include <util/generic/yexception.h>
#include <util/string/split.h>
#include <util/string/vector.h>

namespace {
    class TCacheConfigParserError: public std::exception {
    public:
        TCacheConfigParserError(TStringBuf message) : Message(TString{message}) {}
        const char* what() const noexcept override { return Message.c_str(); }
    private:
        TString Message;
    };

    void SetupCacheDefaults(TDebugOptions* opts) {
        Y_ASSERT(opts != nullptr);

        if (opts->RebuildGraph) {
            opts->ReadFsCache = false;
            opts->WriteFsCache = true;
            opts->ReadDepsCache = false;
            opts->WriteDepsCache = true;
            opts->ReadJsonCache = false;
            // Current strange behavior which should be fixed later
            opts->WriteJsonCache = false;
        }
        if (opts->UseFSCacheOnly) {
            opts->WriteFsCache = true;
            opts->ReadFsCache = true;
            opts->WriteDepsCache = false;
            opts->ReadDepsCache = false;
        }
        if (opts->DontWriteInternalCache) {
            opts->WriteFsCache = false;
            opts->WriteDepsCache = false;
        }
        opts->ReadJsonCache = opts->UseJsonCache && !opts->RebuildJsonGraph;
        opts->WriteJsonCache = opts->UseJsonCache && !opts->DontWriteJsonCache;
    }

    struct TCacheFlags {
        bool ReadConfCache = true;
        bool WriteConfCache = true;
        bool ReadDepsCache = true;
        bool WriteDepsCache = true;
        bool ReadFsCache = true;
        bool WriteFsCache = true;
        bool ReadJsonCache = true;
        bool WriteJsonCache = true;
        bool ReadUidsCache = true;
        bool WriteUidsCache = true;

        bool ConfCacheWasSetExplicitly = false;
        bool UidsCacheWasSetExplicitly = false;
    };

    void ParseCacheConfig(const TVector<TString>& config, TCacheFlags* cacheFlags) {
        auto&& parser = [cacheFlags](TStringBuf item) {
            if (item.size() != 3
                || !EqualToOneOf(item[0], 'c', 'd', 'f', 'j', 'u')
                || item[1] != ':'
                || !EqualToOneOf(item[2], 'a', 'n', 'r', 'w'))
            {
                TStringStream message;
                message << "Invalid value format: [(c|d|f|j|u):(a|n|r|w)]: [" << item << "]";
                throw TCacheConfigParserError(message.Str());
            }

            bool *readFlag = nullptr;
            bool *writeFlag = nullptr;
            switch (item[0]) {
                case 'c':
                    readFlag = &cacheFlags->ReadConfCache;
                    writeFlag = &cacheFlags->WriteConfCache;
                    cacheFlags->ConfCacheWasSetExplicitly = true;
                    break;
                case 'd':
                    readFlag = &cacheFlags->ReadDepsCache;
                    writeFlag = &cacheFlags->WriteDepsCache;
                    break;
                case 'f':
                    readFlag = &cacheFlags->ReadFsCache;
                    writeFlag = &cacheFlags->WriteFsCache;
                    break;
                case 'j':
                    readFlag = &cacheFlags->ReadJsonCache;
                    writeFlag = &cacheFlags->WriteJsonCache;
                    break;
                case 'u':
                    readFlag = &cacheFlags->ReadUidsCache;
                    writeFlag = &cacheFlags->WriteUidsCache;
                    cacheFlags->UidsCacheWasSetExplicitly = true;
                default:
                    // Unreachable code
                    Y_ASSERT(true);
            }
            *readFlag = EqualToOneOf(item[2], 'a', 'r');
            *writeFlag = EqualToOneOf(item[2], 'a', 'w');
        };

        for (const auto& item : config) {
            parser(item);
        }
    }

    void SetupCacheConfig(TDebugOptions* opts) {
        Y_ASSERT(opts != nullptr);

        TCacheFlags flags{
            .ReadConfCache = opts->ReadConfCache,
            .WriteConfCache = opts->WriteConfCache,
            .ReadDepsCache = opts->ReadDepsCache,
            .WriteDepsCache = opts->WriteDepsCache,
            .ReadFsCache = opts->ReadFsCache,
            .WriteFsCache = opts->WriteFsCache,
            .ReadJsonCache = opts->ReadJsonCache,
            .WriteJsonCache = opts->WriteJsonCache,
            .ReadUidsCache = opts->ReadUidsCache,
            .WriteUidsCache = opts->WriteUidsCache,
        };

        try {
            ParseCacheConfig(opts->CacheConfig, &flags);
        } catch (TCacheConfigParserError& e) {
            ythrow yexception() << "Invalid value for --xCC flag: " << e.what();
        }

#define SET_OPT_FLAG(FlagName) { opts->FlagName = flags.FlagName; }
        SET_OPT_FLAG(ReadConfCache);
        SET_OPT_FLAG(WriteConfCache);
        SET_OPT_FLAG(ReadDepsCache);
        SET_OPT_FLAG(WriteDepsCache);
        SET_OPT_FLAG(ReadFsCache);
        SET_OPT_FLAG(WriteFsCache);
        SET_OPT_FLAG(ReadJsonCache);
        SET_OPT_FLAG(WriteJsonCache);
        SET_OPT_FLAG(ReadUidsCache);
        SET_OPT_FLAG(WriteUidsCache);
#undef SET_OPT_FLAG

        opts->ConfCacheWasSetExplicitly = opts->ConfCacheWasSetExplicitly || flags.ConfCacheWasSetExplicitly;
        opts->UidsCacheWasSetExplicitly = opts->UidsCacheWasSetExplicitly || flags.UidsCacheWasSetExplicitly;
    }

    void RestrictCaches(TDebugOptions* opts) {
        Y_ASSERT(opts != nullptr);

        TCacheFlags flags;
        try {
            ParseCacheConfig(opts->RetryConfig, &flags);
        } catch (TCacheConfigParserError& e) {
            ythrow yexception() << "Invalid value for --xRC flag: " << e.what();
        }

#define RESTRICT_OPT_FLAG(FlagName) { opts->FlagName = opts->ReadDepsCache && flags.ReadDepsCache; }
        RESTRICT_OPT_FLAG(ReadConfCache);
        RESTRICT_OPT_FLAG(WriteConfCache);
        RESTRICT_OPT_FLAG(ReadDepsCache);
        RESTRICT_OPT_FLAG(WriteDepsCache);
        RESTRICT_OPT_FLAG(ReadFsCache);
        RESTRICT_OPT_FLAG(WriteFsCache);
        RESTRICT_OPT_FLAG(ReadJsonCache);
        RESTRICT_OPT_FLAG(WriteJsonCache);
        RESTRICT_OPT_FLAG(ReadUidsCache);
        RESTRICT_OPT_FLAG(WriteUidsCache);
#undef RESTRICT_OPT_FLAG

        opts->ConfCacheWasSetExplicitly = opts->ConfCacheWasSetExplicitly || flags.ConfCacheWasSetExplicitly;
        opts->UidsCacheWasSetExplicitly = opts->UidsCacheWasSetExplicitly || flags.UidsCacheWasSetExplicitly;
    }

    void SetupCaches(TDebugOptions* opts) {
        Y_ASSERT(opts != nullptr);

        if (opts->CacheConfig.empty()) {
            SetupCacheDefaults(opts);
        } else {
            SetupCacheConfig(opts);
        }

        if (!opts->RetryConfig.empty()) {
            RestrictCaches(opts);
        }
    }
}

TDebugOptions::EShowExpressionErrors TDebugOptions::ParseShowExpressionErrors(TStringBuf s) {
    if (s == "none")
        return EShowExpressionErrors::None;
    else if (s == "one")
        return EShowExpressionErrors::One;
    else if (s == "all")
        return EShowExpressionErrors::All;
    else
        throw std::runtime_error("unknown expression error detail mode requested");
}

void TDebugOptions::AddOptions(NLastGetopt::TOpts& opts) {
    opts.AddLongOption("xx", "fully rebuild graph").SetFlag(&RebuildGraph).NoArgument();
    opts.AddLongOption("xN", "do not parse source files").SetFlag(&NoParseSrc).NoArgument();
    opts.AddLongOption("xX", "do not use plugins").SetFlag(&DontUsePlugins).NoArgument();
    opts.AddLongOption("xy", "do not rebuild graph when ymake binary changes").SetFlag(&NoChkYMakeChg).NoArgument();
    opts.AddLongOption("xE", "print all data to stderr").SetFlag(&AllToStderr).NoArgument();
    opts.AddLongOption("xL", "dump dependency Loops (cycles) in graph").SetFlag(&ShowLoops).NoArgument();
    opts.AddLongOption("xg", "dump graph (only the part rooted at specified targets)").SetFlag(&DumpGraph).NoArgument();
    opts.AddLongOption("xG", "dump graph (all available nodes)").SetFlag(&FullDumpGraph).NoArgument();
    opts.AddLongOption("xM", "only dump Make-files (for g or G)").SetFlag(&DumpMakefiles).NoArgument();
    opts.AddLongOption("xR", "remove file id's from output (for g or G)").SetFlag(&DumpGraphNoId).NoArgument();
    opts.AddLongOption("xu", "remove file id's and pos from output (for g or G)").SetFlag(&DumpGraphNoPosNoId).NoArgument();
    opts.AddLongOption("xf", "only dump files (for g or G)").SetFlag(&DumpFiles).NoArgument();
    opts.AddLongOption("xd", "only dump directories (for g or G)").SetFlag(&DumpDirs).NoArgument();
    opts.AddLongOption("xm", "only dump modules (for g)").SetFlag(&DumpModules).NoArgument();
    opts.AddLongOption("xc", "only dump recurses").SetFlag(&DumpRecurses).NoArgument();
    opts.AddLongOption("xi", "only dump peerdirs (from all start targets)").SetFlag(&DumpPeers).NoArgument();
    opts.AddLongOption("xz", "dump recurses and peerdirs from start target").SetFlag(&DumpDependentDirs).NoArgument();
    opts.AddLongOption("xe", "also render and dump commands (for g)").SetFlag(&DumpRenderedCmds).NoArgument();
    opts.AddLongOption("dump-expressions", "dump expressions (a.k.a. structured commands)").SetFlag(&DumpExpressions).NoArgument();
    opts.AddLongOption("xb", "also list buildables ordered by depth (for g)").SetFlag(&DumpBuildables).NoArgument();
    opts.AddLongOption("xw", "list buildables with its sources").SetFlag(&DumpTargetDepFiles).NoArgument();
    opts.AddLongOption("xJ", "export graph as JSON").SetFlag(&DumpAsJson).NoArgument();
    opts.AddLongOption("xD", "dump in dot format (for g or G)").SetFlag(&DumpAsDot).NoArgument();
    opts.AddLongOption("xp", "parse all lines entirely").SetFlag(&EntirelyParseFiles).NoArgument();
    opts.AddLongOption("xA", "print build path for first target and exit").SetFlag(&PrintTargetAbsPath).NoArgument();
    opts.AddLongOption("xa", "print all start targets and exit").SetFlag(&PrintTargets).NoArgument();
    opts.AddLongOption("xO", "dump owners").SetFlag(&WriteOwners).NoArgument();
    opts.AddLongOption("xY", "list the contents on Names object").SetFlag(&DumpNames).NoArgument();
    opts.AddLongOption("xI", "also dump indirect peerdir relations (Dir->Module) (for g)").SetFlag(&DumpIndirectPeerdirs).NoArgument();
    opts.AddLongOption("xP", "also use ADDINCL's for traverse (for g)").SetFlag(&DumpAddinclsSubgraphs).NoArgument();
    opts.AddLongOption("xU", "do not traverse RECURSE's in some procedures (like -Z)").SetFlag(&SkipRecurses).NoArgument();
    opts.AddLongOption("xW", "do not traverse all RECURSE's in some procedures").SetFlag(&SkipAllRecurses).NoArgument();
    opts.AddLongOption("xZ", "do not traverse DEPENDS's in some procedures (in dump dependent dirs)").SetFlag(&SkipDepends).NoArgument();
    opts.AddLongOption("xV", "do not traverse tools dependencies in some procedures (in dump dependent dirs)").SetFlag(&SkipTools).NoArgument();
    opts.AddLongOption("xT", "do not traverse ADDINCLs in some procedures").SetFlag(&SkipAddincls).NoArgument();
    opts.AddLongOption("xs", "use experimental json cache").SetFlag(&UseJsonCache).NoArgument();
    opts.AddLongOption("xS", "fully rebuild JSON graph").SetFlag(&RebuildJsonGraph).NoArgument();
    opts.AddLongOption("xB", "dump targets of all includes in graph").SetFlag(&DumpIncludeTargets).NoArgument();
    opts.AddLongOption("xF", "use FS cache without internal graph cache").SetFlag(&UseFSCacheOnly).NoArgument();
    opts.AddLongOption("xh", "dump information about modules").SetFlag(&DumpModulesInfo).NoArgument();
    opts.AddLongOption("xlic", "dump information about licenses").SetFlag(&DumpLicensesInfo).NoArgument();
    opts.AddLongOption("xlic-json", "dump information about licenses machine oriented JSON version").SetFlag(&DumpLicensesMachineInfo).NoArgument();
    opts.AddLongOption("xlic-link-type", "Assume linc type when dumping license property types").StoreResult(&LicenseLinkType);
    opts.AddLongOption("xlic-custom-tag", "dump licenses listed inthe var specified in a separate group").EmplaceTo(&LicenseTagVars);
    opts.AddLongOption("xrecursive", "match modules by prefix from find-path-to (only for all-relations)").SetFlag(&DumpRelationsByPrefix).NoArgument();
    opts.AddLongOption("xshow-targets-deps", "show dependency between target modules (only for all-relations)").SetFlag(&DumpDepsBetweenTargets).NoArgument();
    opts.AddLongOption("xflat-json", "dump graph in flat json format").SetFlag(&DumpGraphFlatJson).NoArgument();
    opts.AddLongOption("xflat-json-with-cmds", "dump graph in flat json format").SetFlag(&DumpGraphFlatJsonWithCmds).NoArgument();
    opts.AddLongOption("xdirect-dm", "dump direct managed peers only").SetFlag(&DumpDirectDM).NoArgument();
    opts.AddLongOption("xsrc-deps", "dump minimized source-level deps. This includes top-most directories and orphaned files.").SetFlag(&DumpSrcDeps).NoArgument();
    opts.AddLongOption("xmkf", "include ya.make files, that are only used as a build configuration (for src-deps)").SetFlag(&WithYaMake).NoArgument();
    opts.AddLongOption("xdump-data", "dump files and directories (w/o expansion) listed in DATA.").SetFlag(&DumpData).NoArgument();
    opts.AddLongOption("xfdm", "dump information forced dependency management").SetFlag(&DumpForcedDependencyManagements).NoArgument();
    opts.AddLongOption("xfdm-json", "dump information forced dependency management as json").SetFlag(&DumpForcedDependencyManagementsAsJson).NoArgument();
    opts.AddLongOption(
        "xcompletely-trust-fs-cache",
        "expect we has updated only files from patch, so trust all cached fs info for files not included in patch"
    ).SetFlag(&CompletelyTrustFSCache).NoArgument();
    opts.AddLongOption("xskip-make-files", "skip make files for dumps").SetFlag(&SkipMakeFilesInDumps).NoArgument();
    opts.AddLongOption("xmark-make-files", "mark make files as makefile for dumps").SetFlag(&MarkMakeFilesInDumps).NoArgument();
    opts.AddLongOption("dump-pretty", "additional formatting in dumps").SetFlag(&DumpPretty).NoArgument();
    opts.AddLongOption("xpatch-path", "use list of changes from patch (.zipatch) or from arc changelist (.cl)").StoreResult(&PatchPath2);
    opts.AddLongOption("xapply-zipatch", "read file content from zipatch").SetFlag(&ReadFileContentFromZipatch2).NoArgument();
    opts.AddLongOption("xexpr-error-details", "show expressions in error messages")
        .RequiredArgument("none|one|all")
        .Handler1([this](const NLastGetopt::TOptsParser* p) {
            ExpressionErrorDetails = ParseShowExpressionErrors(p->CurValStr());
        });
    opts.AddLongOption("xdisable-grand-bypass", "Disable Grand bypass").SetFlag(&DisableGrandBypass).NoArgument();

    auto dontWriteInternalCache = [this](){
        DontWriteInternalCache = true;
    };

    auto dontWriteJsonCache = [this](){
        DontWriteJsonCache = true;
    };

    opts.AddLongOption("xr", "do not write internal graph cache")
        .NoArgument()
        .Handler0(dontWriteInternalCache);
    opts.AddLongOption("dont-save-internal-cache", "do not write internal graph cache")
        .NoArgument()
        .Handler0(dontWriteInternalCache);
    opts.AddLongOption("dont-save-json-cache", "do not write json graph cache")
        .NoArgument()
        .Handler0(dontWriteJsonCache);
    opts.AddLongOption("save-no-cache", "do not write caches")
        .NoArgument()
        .Handler0(dontWriteInternalCache)
        .Handler0(dontWriteJsonCache);
    opts.AddLongOption("dump-file", "Write requested dump to file instead of stdout")
        .StoreResult(&DumpFileName);
    opts.AddLongOption("binary-log-file", "Path to the binary debug log file")
        .StoreResult(&BinaryLogFileName);
    opts.AddLongOption("disable-text-log", "Disable text debug logging to stderr").SetFlag(&DisableTextLog).NoArgument();
    opts.AddLongOption("xCC", "Cache configuration")
        .RequiredArgument("(c|f|d|j|u):(a|r|w|n)[,(c|f|d|j|u):(a|r|w|n)]*")
        .SplitHandler(&CacheConfig, ',');
    opts.AddLongOption("xRC", "Retry ymake with given cache configuration restrictions")
        .RequiredArgument("(c|f|d|j|u):(a|r|w|n)[,(c|f|d|j|u):(a|r|w|n)]*")
        .SplitHandler(&RetryConfig, ',');
}

void TDebugOptions::PostProcess(const TVector<TString>& /* freeArgs */) {
    DumpGraphStuff = DumpGraph | DumpRenderedCmds | DumpBuildables | DumpNames;

    SetupCaches(this);
}
