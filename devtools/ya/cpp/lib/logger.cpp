#include "config.h"
#include "logger.h"
#include "logger_filter.h"

#include <library/cpp/logger/composite.h>
#include <library/cpp/logger/file.h>
#include <library/cpp/logger/filter.h>
#include <library/cpp/logger/null.h>
#include <library/cpp/logger/stream.h>

#include <util/datetime/base.h>
#include <util/random/random.h>
#include <util/generic/array_ref.h>
#include <util/generic/hash_set.h>
#include <util/generic/yexception.h>
#include <util/system/defaults.h>

#include <cctype>

namespace NYa {
    const char* LOG_FILE_NAME_FMT = "%H-%M-%S";
    const char* LOG_DIR_NAME_FMT = "%Y-%m-%d";
    const char* LOG_TIME_FMT = "%Y-%m-%d %H-%M-%S";
    const TString TOKEN_PREFIX_ = "AQAD-";

    namespace NPrivate {
        TString Uid(const size_t len = 16) {
            const TStringBuf symbols = "abcdefjhijklmnopqrstuvwxyz0123456789";
            TString uid(len, 'X');
            for (size_t i = 0; i < len; ++i) {
                uid[i] = symbols[RandomNumber(symbols.size())];
            }
            return uid;
        }

        TStringBuf StripFileName(TStringBuf string) {
            return string.RNextTok(LOCSLASH_C);
        }

        TString LogTime() {
            TInstant now = TInstant::Now();
            return Sprintf("%s,%03ld", now.FormatLocalTime(LOG_TIME_FMT).c_str(), now.MilliSeconds() % 1000);
        }

    } // namespace NPrivate

    using namespace NPrivate;

    struct TYaLogFormatter: public ILoggerFormatter {
        virtual void Format(const TLogRecordContext& ctx, TLogElement& elem) const {
            elem << LogTime() << " " << ToString(ctx.Priority) << " (" << StripFileName(ctx.SourceLocation.File) << ":" << ctx.SourceLocation.Line << ") [CppThread] ";
        };
    };

    TLog& GetLog() {
        return TLoggerOperator<TGlobalLog>::Log();
    }

    void InitLogger(const TFsPath& miscRoot, const TVector<TStringBuf>& args, ELogPriority priority, bool verbose) {
        auto compositeLogBackend = MakeHolder<TCompositeLogBackend>();
        TString fileError;
        try {
            const TInstant now = TInstant::Now();
            const TFsPath logDirPath = miscRoot / "logs" / now.FormatLocalTime(LOG_DIR_NAME_FMT);
            logDirPath.MkDirs();
            const TString logFileName = Sprintf("%s.%s.log", now.FormatLocalTime(LOG_FILE_NAME_FMT).c_str(), Uid().c_str());
            // The generic creator silently falls back to stderr at the file's DEBUG level.
            // Open the file directly so ya can keep terminal diagnostics quiet on failure.
            compositeLogBackend->AddLogBackend(MakeHolder<TFilteredLogBackend>(
                MakeHolder<TFileLogBackend>((logDirPath / logFileName).GetPath()), priority));
        } catch (const TIoException& error) {
            fileError = error.what();
        }
        if (verbose || fileError) {
            const ELogPriority consolePriority = verbose ? priority : Min(priority, TLOG_WARNING);
            compositeLogBackend->AddLogBackend(MakeHolder<TFilteredLogBackend>(
                MakeHolder<TStreamLogBackend>(&Cerr), consolePriority));
        }

        DoInitGlobalLog(THolder(std::move(compositeLogBackend)), MakeHolder<TYaLogFormatter>());

        TLog& log = GetLog();
        log.SetFormatter(TYaTokenFilter(args));
        if (fileError) {
            WARNING_LOG << "Could not create ya log file; using stderr: " << fileError << "\n";
        }
    }

    void InitNullLogger() {
        DoInitGlobalLog(MakeHolder<TNullLogBackend>());
    }
} // namespace NYa
