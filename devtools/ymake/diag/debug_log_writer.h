#pragma once

#include <util/generic/buffer.h>
#include <util/generic/fwd.h>
#include <util/stream/file.h>

class TDebugLogWriter {
public:
    ~TDebugLogWriter();

    void SetFile(TString path);

    template<typename T>
    void Write(const T& event);

private:
    void CheckBufferSize();

private:
    THolder<TFileOutput> Out_;
    TBuffer PreFileOut_;
};

TDebugLogWriter* DebugLogWriter();
