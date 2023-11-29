#include "dbg.h"
#include "debug_log_writer.h"

#if !defined(YMAKE_DEBUG)
#define BINARY_LOG(var, type, ...)
#define DEBUG_USED(...) Y_UNUSED(__VA_ARGS__)
#else
#define BINARY_LOG(var, type, ...) IF_BINARY_LOG(var) { NDebugEvents::type e{__VA_ARGS__}; DebugLogWriter()->Write<NDebugEvents::type>(e); }
#define DEBUG_USED(...)
#endif
