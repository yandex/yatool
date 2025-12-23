#include "logging.h"

#include <devtools/yexport/diag/exception.h>

#include <devtools/ymake/diag/common_msg/msg.ev.pb.h>
#include <devtools/ymake/diag/common_display/trace.h>
#include <devtools/ymake/diag/common_display/trace_sink.h>

#include <util/stream/output.h>
#include <util/stream/file.h>
#include <util/system/file.h>
#include <util/system/mutex.h>

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_sinks.h>

namespace {

    class TDefaultTracer: public NYMake::ITraceSink {
    public:
        void Trace(const TString& ev) override {
            std::lock_guard guard{spdlog::details::console_mutex::mutex()};
            Cerr << ev << Endl;
        }
    };

    class TFileTracer: public NYMake::ITraceSink {
    public:
        TFileTracer(const fs::path& logPath)
            : LogFileStream_(TFile(logPath.c_str(), CreateAlways))
        {}
        void Trace(const TString& ev) override {
            TGuard g(Mutex_);
            LogFileStream_ << ev.c_str() << Endl;
        }

    private:
        TMutex Mutex_;
        TFileOutput LogFileStream_;
    };

    class TEvlogSink: public spdlog::sinks::sink {
    public:
        TEvlogSink() {
            set_level(spdlog::level::warn);
        }

        void flush() override {}
        virtual void set_pattern(const std::string &) override {};
        virtual void set_formatter(std::unique_ptr<spdlog::formatter>) override {};

        void log(const spdlog::details::log_msg &msg) override {
            NEvent::TDisplayMessage evmsg;
            evmsg.SetMessage(TString{msg.payload.data(), msg.payload.size()});
            switch (msg.level) {
                case spdlog::level::off:
                case spdlog::level::n_levels:
                    evmsg.SetType("Warn");
                    break;

                case spdlog::level::trace:
                case spdlog::level::debug:
                    evmsg.SetType("Debug");
                    break;
                case spdlog::level::info:
                    evmsg.SetType("Info");
                    break;
                case spdlog::level::warn:
                    evmsg.SetType("Warn");
                    break;
                case spdlog::level::critical:
                case spdlog::level::err:
                    evmsg.SetType("Error");
                    break;
            }
            NYMake::Trace(evmsg);
        }
    };


    class TFailOnErrorSink: public spdlog::sinks::sink {
    public:
        TFailOnErrorSink() {
            set_level(spdlog::level::err);
        }

        void flush() override {}
        virtual void set_pattern(const std::string &) override {};
        virtual void set_formatter(std::unique_ptr<spdlog::formatter>) override {};

        void log(const spdlog::details::log_msg &msg) override {
            switch (msg.level) {
                case spdlog::level::critical:
                case spdlog::level::err:
                    isFailOnError_ = true;
                    break;
                default:
                    break;
            }
        }
        static bool IsFailOnError() {
            return isFailOnError_;
        }

    private:
        static bool isFailOnError_;
    };

    bool TFailOnErrorSink::isFailOnError_ = false; // non-const static data member must be initialized out of line
}

namespace NYexport {

void SetupLogger(TLoggingOpts opts) {
    spdlog::default_logger()->sinks() = {};
    if (opts.EnableStderr) {
        spdlog::default_logger()->sinks().push_back(std::make_shared<spdlog::sinks::stderr_sink_mt>());
    }
    if (opts.EnableEvlog) {
        spdlog::default_logger()->sinks().push_back(std::make_shared<TEvlogSink>());
    }
    if (opts.FailOnError) {
        spdlog::default_logger()->sinks().push_back(std::make_shared<TFailOnErrorSink>());
    }
    spdlog::cfg::load_env_levels();

    static THolder<NYMake::ITraceSink> traceSink;
    if (!opts.EvLogFilePath.empty()) {
        try {
            traceSink = MakeHolder<NYMake::TScopedTraceSink<TFileTracer>>(opts.EvLogFilePath);
        } catch (const std::exception& ex) {
            YEXPORT_THROW("Failed to open evlog file due to: " << ex.what());
        }
    } else {
        traceSink = MakeHolder<NYMake::TScopedTraceSink<TDefaultTracer>>();
    }
}

bool IsFailOnError() {
    return TFailOnErrorSink::IsFailOnError();
}

}
