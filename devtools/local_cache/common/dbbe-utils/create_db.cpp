#include "create_db.h"

#include <util/string/cast.h>
#include <util/system/fs.h>

void NCachesPrivate::SaveDb(const TString& dbPath, TLog& log, const char* emergencyMsg) {
    LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_EMERG, emergencyMsg) << "Will save '" << dbPath << "' to '" << dbPath << ".corrupted and start with clean file." << Endl;
    using namespace NFs;
    // ignore errors
    TStringBuf fsDbPath(dbPath);
    if (fsDbPath.StartsWith("file:")) {
        TStringBuf scheme, path;
        fsDbPath.Split(':', scheme, path);
        fsDbPath = path;
    }
    for (auto suff : {"", "-wal", "-shm"}) {
        Remove(ToString(fsDbPath) + suff + ".corrupted");
        Rename(ToString(fsDbPath) + suff, ToString(fsDbPath) + suff + ".corrupted");
    }
    // EO ignore errors
}
