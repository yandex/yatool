from devtools.ya.core.error import ExitCodes as YaExitCodes

from . import errors


UNRETRIABLE_ERRORS = [
    errors.DistbuildRepositoryError,
    errors.DistbuildResourceError,
    errors.DistbuildRemoteCommandError,
    errors.DistbuildAuthError,
    errors.DistbuildNonRetriableError,
    errors.FastCircuitError,
    errors.SandboxResourceError,
    errors.YaArgsBindingError,
    errors.YaConfError,
    errors.YcmergeError,
    errors.YmakeCrashedError,
    errors.YmakeFailedError,
    errors.YMakeConfigureError,
    errors.ZipatchApplyError,
    errors.PythonSegmentationFaultError,
]

RETRIABLE_ERRORS = [
    errors.SocketConnectionError,
    errors.TransportConnectionError,
    errors.YaFileNotFound,
    errors.QuotaExceededError,
    errors.DiskQuotaExceededError,
    errors.NoSpaceLeftOnDeviceError,
    errors.PermissionDeniedError,
    errors.YaRuntimeError,
    errors.YaRunnerLateImportFlakyError,
]

UNKNOWN_ERRORS = [
    errors.AutocheckTokenError,
    errors.PythonBuildInErrors,
]

RETRIABLE_YA_EXIT_CODES = frozenset([YaExitCodes.INFRASTRUCTURE_ERROR])

BROKEN_HOST_ENVIRONMENT_YA_EXIT_CODES = frozenset([
    -7,  # signal.SIGBUS
    -11,  # signal.SIGSEGV
    126,  # https://a.yandex-team.ru/tasklets/namespaces/pci_express_tasks/pci_express_legs/run/74ee234d-0ce9-4183-a87b-40a7d117f0c9?ymExecutionStatus=success&tab=logs&components=stderr%2Cstdout
    127,
])

BROKEN_HOST_ENVIRONMENT_ERRORS = frozenset([
    errors.FailedToReadDirDuringConfInitializationError,
    errors.IOErrorFromYmakeConf,
])
