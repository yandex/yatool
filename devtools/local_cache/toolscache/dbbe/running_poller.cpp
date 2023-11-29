#include "devtools/local_cache/toolscache/dbbei.h"

#include "devtools/local_cache/toolscache/db/db-public.h"
#include "devtools/local_cache/common/dbbe-running-procs/running_handler_impl.h"

template class NCachesPrivate::TRunningProcsHandler<NToolsCache::TGCAndFSHandler, NToolsCachePrivate::TRunningQueriesStmts>;
