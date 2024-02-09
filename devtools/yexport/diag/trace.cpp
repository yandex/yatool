#include "trace.h"

#include <devtools/yexport/diag/msg/msg.ev.pb.h>
#include <devtools/ymake/diag/common_display/trace.h>

namespace NYexport {

void TraceFileExported(const fs::path& path) {
    NYMake::Trace(NEvent::TFileExported(path.c_str()));
}

void TracePathRemoved(const fs::path& path) {
    NYMake::Trace(NEvent::TPathRemoved(path.c_str()));
}

}
