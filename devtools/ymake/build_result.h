#pragma once

enum EBuildResult {
    BR_OK = 0,
    BR_FATAL_ERROR = 1,
    BR_CONFIGURE_FAILED = 2,
    BR_RETRYABLE_ERROR = 3,
    BR_INTERRUPTED = 130 // similar to unix exit code or 128 + SIGINT(2)
};
