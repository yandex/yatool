#include "display.h"
#include "trace.h"

#include <devtools/ymake/context_executor.h>

#include <library/cpp/json/json_value.h>
#include <library/cpp/json/json_writer.h>

#include <library/cpp/protobuf/json/proto2json.h>

#include <util/datetime/base.h>

#include <util/generic/singleton.h>

namespace NCommonDisplay{
    namespace {
        class TStdOutput: public IOutputStream {
        public:
            inline TStdOutput(FILE* f) noexcept
                : F_(f)
            {
            }

            ~TStdOutput() override {
            }

        private:
            void DoWrite(const void* buf, size_t len) override {
                if (len != fwrite(buf, 1, len, F_)) {
                    ythrow TSystemError() << "write failed";
                }
            }

            void DoFlush() override {
                fflush(F_);
            }

        private:
            FILE* F_;
        };
    }

    TLockedStream::TLockedStream()
            : Output(new TStdOutput(stderr))
    {
    }

    void TLockedStream::SetStream(TAtomicSharedPtr<IOutputStream> stream) {
        with_lock (Lock) {
            Output.Reset(stream);
        }
    }

    void TLockedStream::Emit(const TStringBuf& str) {
        with_lock (Lock) {
            (*Output << str).Flush();
        }
    }

    TLockedStream* LockedStream() {
        auto ctx = CurrentContext<TExecContext>;
        if (ctx && ctx->LockedStream) {
            return ctx->LockedStream.get();
        }
        return Singleton<TLockedStream>();
    }

    void DisplayMsg(const TProtoMessage& msg) {
        LockedStream()->Emit(NYMake::EventToStr(msg));
    }
}
