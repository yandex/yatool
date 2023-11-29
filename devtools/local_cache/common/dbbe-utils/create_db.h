#pragma once

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>
#include <library/cpp/sqlite3/sqlite.h>

namespace NCachesPrivate {
    void SaveDb(const TString& dbPath, TLog& log, const char* emergencyMsg);

    // Return open connection for in-memory DBs.
    template <typename Func>
    THolder<NSQLite::TSQLiteDB> CreateDBIfNeeded(const TString& dbPath, TLog& log, Func&& prepareStmts, const char* emergencyMsg, bool& recreate) {
        constexpr int ATTEMPTS = 2;
        THolder<NSQLite::TSQLiteDB> db;
        if (recreate) {
            SaveDb(dbPath, log, emergencyMsg);
        }
        // - single attempt to save broken DB;
        // - any number of attempts for SQLITE_LOCKED/SQLITE_BUSY;
        // - no retries for unknown issues.
        for (int i = 0; i < ATTEMPTS;) {
            try {
                db.Reset(new NSQLite::TSQLiteDB(dbPath, SQLITE_OPEN_URI | SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX));
                prepareStmts(*db);
                break;
            } catch (const NSQLite::TSQLiteError& e) {
                auto err = e.GetErrorCode();
                bool retry = false;
                if ((err == SQLITE_CORRUPT || err == SQLITE_NOTADB) && i + 1 != ATTEMPTS) {
                    LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_EMERG, emergencyMsg) << "DB file corrupted or not a SQLite. " << Endl;
                    retry = true;
                }
                if ((err == SQLITE_IOERR) && i + 1 != ATTEMPTS) {
                    LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_EMERG, emergencyMsg) << "IO occurred while accessing SQLite DB file. " << Endl;
                    retry = true;
                }
                if ((err == SQLITE_INTERNAL || err == SQLITE_ERROR) && i + 1 != ATTEMPTS) {
                    LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_EMERG, emergencyMsg) << "DB is inconsistent and/or scheme issue." << Endl;
                    retry = true;
                }

                if (retry) {
                    recreate = true;
                    SaveDb(dbPath, log, emergencyMsg);
                    ++i;
                    continue; // another attempt
                }

                if (err != SQLITE_LOCKED && err != SQLITE_BUSY) {
                    throw;
                }
            }
        }
        return db;
    }
}
