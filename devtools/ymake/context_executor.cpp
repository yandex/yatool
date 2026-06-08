#include "context_executor.h"

#include <devtools/ymake/diag/mod_stats_manager.h>

TExecContext::~TExecContext() {
    ModStatsManager->CheckUnreported();
}
