#include "cleaner.h"

#include <library/cpp/getopt/last_getopt.h>

#include <util/generic/size_literals.h>
#include <util/stream/file.h>
#include <util/string/strip.h>
#include <util/system/env.h>


const TString DEFAULT_YT_METADATA_TABLE = "metadata";
const TString DEFAULT_YT_DATA_TABLE = "data";
constexpr i64 DEFAULT_YT_JOB_MEMORY_LIMIT = 16_GB;

struct TCommandLineOptsValue {
    TString YtProxy;
    TString YtToken;
    TString YtTokenPath;
    TString YtDir;
    TString YtMetadataTable;
    TString YtDataTable;
    i64 YtJobMemoryLimit;
    bool DryRun;
};

struct TCommandLineError: public yexception { };

void PrintUsage(const TString& progName) {
    Cerr <<
        "DEPRECATED. Use 'ya tool yt data-gc' instead\n\n"
        "Usage: " + progName + " OPTIONS\n"
        "  OPTIONS:\n"
        "    --yt-proxy PROXY           YT proxy url. If missed YT_PROXY env variable is used\n"
        "    --yt-token TOKEN           YT auth token. If missed YT_TOKEN env variable is used\n"
        "    --yt-token-path FILE       YT auth token file path. If missed YT_TOKEN_PATH env variable is used\n"
        "    --yt-dir PATH              YT storage path\n"
        "    --yt-metadata-table TABLE  YT storage path. Default: '" << DEFAULT_YT_METADATA_TABLE << "'\n"
        "    --yt-data-table TABLE      YT storage path. Default: '" << DEFAULT_YT_DATA_TABLE << "'\n"
        "    -n, --dry-run              Don't delete found orphan rows\n"
        "\n";
}

TString GetYtToken(const TString& ytTokenPath, const TString& ytToken) {
    if (!ytToken.empty())
        return ytToken;
    TString token = TUnbufferedFileInput(ytTokenPath).ReadAll();
    return StripStringRight(token);
}

TCommandLineOptsValue ParseCommandLine(int argc, const char* argv[]) {
    TCommandLineOptsValue optsValue;

    auto opts = NLastGetopt::TOpts();
    opts.SetTitle("Clean data table of orphan rows");
    opts.AddLongOption("help", "print usage").NoArgument();
    opts.AddLongOption("yt-proxy").RequiredArgument().StoreResult(&optsValue.YtProxy).DefaultValue(GetEnv("YT_PROXY"));
    opts.AddLongOption("yt-token-path").RequiredArgument().StoreResult(&optsValue.YtTokenPath).DefaultValue(GetEnv("YT_TOKEN_PATH"));
    opts.AddLongOption("yt-token").RequiredArgument().StoreResult(&optsValue.YtToken).DefaultValue(GetEnv("YT_TOKEN"));
    opts.AddLongOption("yt-dir").RequiredArgument().StoreResult(&optsValue.YtDir);
    opts.AddLongOption("yt-metadata-table").RequiredArgument().StoreResult(&optsValue.YtMetadataTable).DefaultValue(DEFAULT_YT_METADATA_TABLE);
    opts.AddLongOption("yt-data-table").RequiredArgument().StoreResult(&optsValue.YtDataTable).DefaultValue(DEFAULT_YT_DATA_TABLE);
    opts.AddLongOption("yt-job-memory-limit").RequiredArgument().StoreResult(&optsValue.YtJobMemoryLimit).DefaultValue(DEFAULT_YT_JOB_MEMORY_LIMIT);
    opts.AddLongOption('n', "dry-run").StoreTrue(&optsValue.DryRun);
    opts.SetFreeArgsMax(0);

    NLastGetopt::TOptsParseResult optsResult(&opts, argc, argv);

    if (optsResult.Has("help")) {
        const TString progName(argv[0]);
        PrintUsage(progName);
        exit(1);
    }

    // Check params
    if (optsValue.YtProxy.empty()) {
        throw TCommandLineError() << "Specify '--yt-proxy' option or YT_PROXY evironment";
    }
    if (optsValue.YtToken.empty() && optsValue.YtTokenPath.empty()) {
        throw TCommandLineError() << "'Specify one of '--yt-token' or '--yt-token-path' options or set appropriate env variable (YT_TOKEN or YT_TOKEN_PATH)";
    }
    if (optsValue.YtDir.empty()) {
        throw TCommandLineError() << "Specify '--yt-dir' option";
    }

    return optsValue;
}

int RealMain(int argc, const char* argv[]) {
    YtJobEntry(argc, argv);

    TCommandLineOptsValue optsValue = ParseCommandLine(argc, argv);
    TString ytToken = GetYtToken(optsValue.YtTokenPath, optsValue.YtToken);

    DoCleanData(optsValue.YtProxy, ytToken, optsValue.YtDir, optsValue.YtMetadataTable, optsValue.YtDataTable, optsValue.YtJobMemoryLimit, optsValue.DryRun);

    return 0;
}

int main(int argc, const char* argv[]) {
    try {
        return RealMain(argc, argv);
    }
    catch (TCommandLineError& err) {
        Cerr << err.what() << Endl;
        return 1;
    }
}
