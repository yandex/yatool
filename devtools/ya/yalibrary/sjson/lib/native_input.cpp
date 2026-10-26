#include "native_input.h"

#include <util/generic/yexception.h>

namespace NSJson {
    namespace {
        class TReleaseGIL {
        public:
            TReleaseGIL()
                : State_(PyEval_SaveThread())
            {
            }

            ~TReleaseGIL() {
                PyEval_RestoreThread(State_);
            }

        private:
            TReleaseGIL(const TReleaseGIL&) = delete;
            TReleaseGIL& operator=(const TReleaseGIL&) = delete;
            PyThreadState* State_;
        };
    } // namespace

    TNativeFileInput::TNativeFileInput(const TString& path)
        : File_(path)
    {
        if (TStringBuf(path).EndsWith(".uc")) {
            Decoded_ = MakeHolder<NUCompress::TDecodedInput>(&File_);
        }
    }

    size_t TNativeFileInput::DoRead(void* data, size_t size) {
        size_t result;
        {
            // The decoder and file I/O use no Python objects. Restore the GIL
            // on both normal return and exceptions before invoking callbacks.
            TReleaseGIL release;
            result = Decoded_ ? Decoded_->Read(data, size) : File_.Read(data, size);
        }
        if (PyErr_CheckSignals() < 0) {
            ythrow yexception() << "Native JSON input interrupted";
        }
        return result;
    }
} // namespace NSJson
