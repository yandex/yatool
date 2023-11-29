#include "debug_log_writer.h"

#include <util/stream/buffer.h>

template<typename T>
static inline void SaveEvent(IOutputStream* stream, const T& event);

template<typename T>
void TDebugLogWriter::Write(const T& event) {
    if (Out_) {
        SaveEvent<T>(Out_.Get(), event);
    } else {
        CheckBufferSize();
        TBufferOutput bufferOut{PreFileOut_};
        SaveEvent(&bufferOut, event);
    }
}
