# Generated by devtools/yamaker.

LIBRARY()

LICENSE(Apache-2.0)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(20240722.0)

PEERDIR(
    contrib/restricted/abseil-cpp-tstring/y_absl/base
    contrib/restricted/abseil-cpp-tstring/y_absl/container
    contrib/restricted/abseil-cpp-tstring/y_absl/debugging
    contrib/restricted/abseil-cpp-tstring/y_absl/flags
    contrib/restricted/abseil-cpp-tstring/y_absl/hash
    contrib/restricted/abseil-cpp-tstring/y_absl/numeric
    contrib/restricted/abseil-cpp-tstring/y_absl/profiling
    contrib/restricted/abseil-cpp-tstring/y_absl/strings
    contrib/restricted/abseil-cpp-tstring/y_absl/synchronization
    contrib/restricted/abseil-cpp-tstring/y_absl/time
    contrib/restricted/abseil-cpp-tstring/y_absl/types
)

ADDINCL(
    GLOBAL contrib/restricted/abseil-cpp-tstring
)

IF (OS_ANDROID)
    LDFLAGS(-llog)
ENDIF()

NO_COMPILER_WARNINGS()

IF(Y_ABSL_DONT_USE_DEBUG)
    CFLAGS(-DY_ABSL_DONT_USE_DEBUG_LIBRARY=1)
ENDIF()

SRCS(
    die_if_null.cc
    flags.cc
    globals.cc
    initialize.cc
    internal/check_op.cc
    internal/conditions.cc
    internal/fnmatch.cc
    internal/globals.cc
    internal/log_format.cc
    internal/log_message.cc
    internal/log_sink_set.cc
    internal/nullguard.cc
    internal/proto.cc
    internal/vlog_config.cc
    log_entry.cc
    log_sink.cc
)

END()