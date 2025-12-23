#pragma once

#include "diag.h"
#include "display.h"

#include <util/stream/output.h>
#include <util/stream/null.h>
#include <util/generic/fwd.h>
#include <util/generic/yexception.h>
#include <util/generic/ylimits.h>

/// general subsystem error
class TError: public yexception {
};

class TConfigurationError: public yexception {
};

class TRuntimeAssertion: public TError {
};

class TNotImplemented: public yexception {
};

class TInvalidGraph: public yexception {
};

#define AssertEx(COND, MSG)                                                           \
    if (!(COND))                                                                      \
        ythrow TRuntimeAssertion() << "Assertion failed: " << #COND << " -- " << MSG; \
    else {                                                                            \
    }

#define CheckEx(COND, MSG)   \
    if (Y_LIKELY(COND)) { \
    } else                   \
    throw TError() << MSG

/// general build error caused by bad sources or environment
class TMakeError: public TError {
};

struct TEatStream {
    Y_FORCE_INLINE bool operator|(const IOutputStream&) const {
        return true;
    }
};

// use YDIAG for debug messages that are not intended for the users
// * it simply prints to stderr with "var:" prefix
// * prints only in debug build
//   (use relwithdebinfo if you need both performance and these messages)
// * these messages are not persisted in cache
// * ya-bin usually hides them, use -v or run ymake directly

#if !defined(NDEBUG) && !defined(YMAKE_DEBUG)
#define YMAKE_DEBUG
#endif

#if !defined(YMAKE_DEBUG)
#define YDIAG(var) false && TEatStream() | Cnull
#define IF_BINARY_LOG(var) if (false)
#else
#define YDIAG(var) (Y_UNLIKELY(Diag()->TextLog && Diag()->var)) && TEatStream() | Cerr << #var ": "
#define IF_BINARY_LOG(var) if (Y_UNLIKELY(Diag()->BinaryLog && Diag()->var))
#endif

class TNonDebugEmpty {};

#if !defined(YMAKE_DEBUG)
static constexpr bool DebugEnabled = false;

template<typename TBase, typename TNonDebugBase = TNonDebugEmpty>
class TDebugOnly : public TNonDebugBase {
public:
    template<typename... TArgs>
    TDebugOnly(const TArgs&... args) {
        Y_UNUSED(args...);
    }

    TDebugOnly(const TDebugOnly& other) { Y_UNUSED(other); }
    TDebugOnly& operator=(const TDebugOnly& other) { Y_UNUSED(other); return *this; }
};
#else
static constexpr bool DebugEnabled = true;

template<typename TBase, typename TNonDebugBase = TNonDebugEmpty>
class TDebugOnly : public TBase {
public:
    using TBase::TBase;
};
#endif

void AssertNoCall();
