#include "logging.h"

#include <devtools/ymake/diag/common_msg/msg.ev.pb.h>
#include <devtools/ymake/diag/common_display/trace.h>
#include <devtools/ymake/diag/common_display/trace_sink.h>

#include <util/stream/output.h>

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_sinks.h>

namespace {

    class TTracer: public NYMake::ITraceSink {
    public:
        void Trace(const TString& ev) override {
            std::lock_guard guard{spdlog::details::console_mutex::mutex()};
            Cerr << ev << Endl;
        }
    };
    NYMake::TScopedTraceSink<TTracer> GLOBAL_TRACER;

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

}

void SetupLogger(TLoggingOpts opts) {
    spdlog::default_logger()->sinks() = {};
    if (opts.EnableStderr)
        spdlog::default_logger()->sinks().push_back(std::make_shared<spdlog::sinks::stderr_sink_mt>());
    if (opts.EnableEvlog) {
        spdlog::default_logger()->sinks().push_back(std::make_shared<TEvlogSink>());
    }
    spdlog::cfg::load_env_levels();
}
