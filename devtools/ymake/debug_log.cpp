#include "debug_log_events.h"

#include <devtools/ymake/diag/debug_log_writer_impl.h>

#include <util/generic/va_args.h>

template<typename T>
static inline void SaveEvent(IOutputStream* stream, const T& event) {
    TDebugLogEvent eventWrapper{event};
    Save(stream, eventWrapper);
}

#define INSTANTIATE_WRITE(type) template void TDebugLogWriter::Write<type>(const type&);

Y_MAP_ARGS(INSTANTIATE_WRITE, DEBUG_EVENT_TYPES)
