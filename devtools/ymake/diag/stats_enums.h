#pragma once

namespace NStats {
    enum class EModulesStats {
        Accessed /* "accessed" */,
        Loaded /* "loaded" */,
        Outdated /* "outdated" */,
        Parsed /* "parsed" */,
        Total /* "total" */,
    };

    enum class EMakeCommandStats {
        InitModuleEnvCalls /* "module environment initialization calls" */,
        InitModuleEnv      /* "actual module environment initializations" */,
    };

    enum class EUpdIterStats {
        NukedDir /* "nuke mod dir" */,
    };

    enum class EGeneralParserStats {
        Count /* "count" */,
        Includes /* "includes count" */,
        UniqueCount  /* "unique count" */,
        Size /* "size" */,
        UniqueSize  /* "unique size" */,
    };

    enum class EIncParserManagerStats {
        ParseTime /* "parse time" */,
        ParsedFilesCount /* "parsed files count" */,
        ParsedFilesSize /* "parsed files size" */,
        ParsedFilesRecovered /* "parsed files recovered" */,
        InFilesCount /* ".in files count" */,
        InFilesSize /* ".in files size" */,
    };

    enum class EFileConfStats {
        LoadedSize /* "loaded size" */,
        LoadTime /* "load time" */,
        LoadedMD5Time /* "loaded MD5 time" */,
        MaxLoadedMD5Time /* "Max loaded MD5 time" */,
        LoadedCount /* "loaded count" */,
        MaxLoadTime /* "max load time" */,
        MappedSize /* "mapped size" */,
        MappedMD5Time /* "mapped MD5 time" */,
        MaxMappedMD5Time /* "Max mapped MD5 time" */,
        MappedCount /* "mapped count" */,
        MapTime /* "map time" */,
        FromPatchCount /* "from patch count" */,
        FromPatchSize /* "from patch size" */,
        FileStatCount /* "file stat count" */,
        LstatCount /* "lstat count" */,
        LstatSumUs /* "lstat sum us" */,
        LstatMinUs /* "lstat min us" */,
        LstatAvrUs /* "lstat avr us" */,
        LstatMaxUs /* "lstat max us" */,
        OpendirCount /* "opendir count" */,
        OpendirSumUs /* "opendir sum us" */,
        OpendirMinUs /* "opendir min us" */,
        OpendirAvrUs /* "opendir avr us" */,
        OpendirMaxUs /* "opendir max us" */,
        ReaddirCount /* "readdir count" */,
        ReaddirSumUs /* "readdir sum us" */,
        ReaddirMinUs /* "readdir min us" */,
        ReaddirAvrUs /* "readdir avr us" */,
        ReaddirMaxUs /* "readdir max us" */,
        ListDirSumUs /* "(opendir + readdir) sum us" */,
        LstatListDirSumUs /* "(lstat + opendir + readdir) sum us" */,
    };

    enum class EFileConfSubStats {
        BucketId /* "bucket id" */,
        LoadedSize /* "loaded size" */,
        LoadTime /* "load time" */,
        LoadedCount /* "loaded count" */,
        MaxLoadTime /* "max load time" */,
    };

    enum class EDepGraphStats {
        NodesCount /* "nodes count" */,
        EdgesCount /* "edges count" */,
        FilesCount /* "files count" */,
        CommandsCount /* "commands count" */,
    };

    enum class EInternalCacheSaverStats {
        TotalCacheSize /* "Total cache size on save" */,
        DiagnosticsCacheSize /* "Diagnostics cache size on save" */,
        GraphCacheSize /* "Graph cache size on save" */,
        ParsersCacheSize /* "Parsers cache size on save" */,
        ModulesTableSize /* "Modules table size on save" */,
        TimesTableSize /* "Times table size on save" */,
        NamesTableSize /* "Names table size on save" */,
        CommandsSize /* "Commands cache size on save" */,
    };

    enum class EJsonCacheStats {
        LoadedItems /* "Loaded cache items" */,
        AddedItems /* "Added cache items" */,
        OldItemsSaved /* "Saved old cache items" */,
        NewItemsSaved /* "Saved new cache items" */,
        TotalItemsSaved /* "Saved total cache items" */,

        FullMatchLoadedItems /* "Loaded full match items" */,
        FullMatchRequests /* "Full match requests" */,
        FullMatchSuccess /* "Successful full matches" */,

        PartialMatchLoadedItems /* "Loaded partial match items" */,
        PartialMatchRequests /* "Partial match requests" */,
        PartialMatchSuccess /* "Successful partial matches" */,

        FullyRendered /* "Fully rendered nodes" */,
        PartiallyRendered /* "Partially rendered nodes" */,
        NoRendered /* "Nodes restored without rendering" */,
    };

    enum class EResolveStats {
        IncludesAttempted /* "Includes attempts" */,
        IncludesFromCache /* "From cache" */,
        ResolveAsKnownTotal /* "Resolve AsKnown total" */,
        ResolveAsKnownFromCache /* "Resolve AsKnown from cache" */,
    };

    enum class EUidsCacheStats {
        LoadedNodes /* "Loaded nodes" */,
        SkippedNodes /* "Skipped nodes" */,
        DiscardedNodes /* "Discarded nodes" */,
        LoadedLoops /* "Loaded loops" */,
        SkippedLoops /* "Skipped loops" */,
        DiscardedLoops /* "Discarded loops" */,
        SavedNodes /* "Saved nodes" */,
        SavedLoops /* "Saved loops" */,
        ReallyAllNoRendered /* "Really all nodes no rendered" */,
    };

    enum class EYMakeStats {
        WriteJSONTime /* "Write JSON time, sec" */,
        RenderJSONTime /* "Render JSON time, sec" */,
        RunTime /* "Run time, sec" */,
        ConfigureGraphTime /* "Configure graph time, sec" */,
        ConfigureReadonlyGraphTime /* "Configure readonly graph time, sec" */,
        UsedConfCache /* "Used Conf cache, bool" */,
        UsedFSCache /* "Used FS cache, bool" */,
        UsedDepsCache /* "Used Deps cache, bool" */,
        UsedJSONCache /* "Used JSON cache, bool" */,
        UsedUidsCache /* "Used Uids cache, bool" */,
        BadLoops /* "Bad loops were detected, bool" */,
    };
}
