#include "debug_log_writer.h"

#include <util/generic/size_literals.h>

#include <type_traits>

constexpr ui64 DebugOutPreFileSize = 1_MB;

namespace {
    void AbortPreFileSizeExceeded() {
        Cerr << "Debug log output has exceeded its size limit while no output file is set" << Endl;
        std::terminate();
    }

    void AbortPreFileNotSaved() {
        Cerr << "Non-empty debug log output will be lost as no output file was ever set" << Endl;
        std::terminate();
    }
}

TDebugLogWriter::~TDebugLogWriter() {
    if (!PreFileOut_.Empty()) {
        AbortPreFileNotSaved();
    }
}

void TDebugLogWriter::SetFile(TString path) {
    Y_ABORT_UNLESS(!Out_);
    Out_.Reset(new TFileOutput{path});
    if (!PreFileOut_.Empty()) {
        Out_->Write(PreFileOut_.Data(), PreFileOut_.Size());
        PreFileOut_.Reset();
    }
}

void TDebugLogWriter::CheckBufferSize() {
    if (PreFileOut_.Size() >= DebugOutPreFileSize) {
        AbortPreFileSizeExceeded();
    }
}

TDebugLogWriter* DebugLogWriter() {
    return Singleton<TDebugLogWriter>();
}
