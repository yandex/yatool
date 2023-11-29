#include "devtools/local_cache/ac/db/db-public.h"
#include "devtools/local_cache/ac/dbbei.h"
#include "devtools/local_cache/common/dbbe-running-procs/running_handler_impl.h"

template class NCachesPrivate::TRunningProcsHandler<NACCache::TIntegrityHandler, NACCachePrivate::TRunningQueriesStmts>;
