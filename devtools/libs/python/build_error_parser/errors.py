import re

from . import error_base


class DistbuildRepositoryError(error_base.Error):
    ERROR_RE = re.compile('Repository acquisition failed')
    MESSAGE = 'Failed to prepare repository on distbuild'


class DistbuildResourceError(error_base.Error):
    ERROR_RE = re.compile('Resource acquisition failed')
    MESSAGE = 'Failed to prepare resources on distbuild'


class DistbuildRemoteCommandError(error_base.Error):
    ERROR_RE = re.compile('RemoteCommandError:')
    MESSAGE = 'Failed to execute command on distbuild'


class DistbuildAuthError(error_base.Error):
    ERROR_RE = re.compile('Build is failed, error: Build rejected: Unknown tree')
    MESSAGE = 'Failed to authenticate user on distbuild'


class DistbuildNonRetriableError(error_base.Error):
    ERROR_RE = re.compile('NotRetriableDistbsException:')
    MESSAGE = 'Failed to execute build on distbuild'


class FastCircuitError(error_base.Error):
    ERROR_RE = re.compile('Fail fast circuit')
    MESSAGE = 'Fast circuit failed'


class SandboxResourceError(error_base.Error):
    ERROR_RE = re.compile('Invalid sandbox resource:')
    MESSAGE = 'Invalid sandbox resource'


class YaArgsBindingError(error_base.Error):
    ERROR_RE = re.compile('ArgsBindingException: Do not know what to do with')
    MESSAGE = 'Unknown arguments in the `ya make` command'


class YaConfError(error_base.Error):
    JSON_EXCEPTIONS = [
        r'ValueError: library\/python\/json\/loads\.cpp',
        'JSONDecodeError:',
    ]

    ERROR_RE = re.compile('|'.join(JSON_EXCEPTIONS), re.MULTILINE)
    MESSAGE = 'ya failed to load ya.conf.json'


class YcmergeError(error_base.Error):
    ERROR_RE = re.compile('ycmerge: error:')
    MESSAGE = 'ycmerge failed'


class YmakeCrashedError(error_base.Error):
    ERROR_RE = re.compile('YMake crashed')
    MESSAGE = 'ymake crashed'


class YmakeFailedError(error_base.Error):
    ERROR_RE = re.compile('YMake failed with exit code 1')
    MESSAGE = 'ymake failed to build graph'


class YMakeConfigureError(error_base.Error):
    ERROR_RE = re.compile(r'YMakeConfigureError: Configure error \(use -k to proceed\)')
    MESSAGE = 'Configure error - graph cannot be built due to errors in ya.make'


class ZipatchApplyError(error_base.Error):
    ERROR_RE = re.compile('ZipatchMalformedError:')
    MESSAGE = 'Failed to apply a patch to the repository'


class SocketConnectionError(error_base.Error):
    ERROR_RE = re.compile(r'\[Errno 107\] Socket not connected')
    MESSAGE = 'Socket not connected error'


class TransportConnectionError(error_base.Error):
    ERROR_RE = re.compile(r'\[Errno 107\] Transport endpoint is not connected')
    MESSAGE = 'Transport endpoint is not connected'


class YaFileNotFound(error_base.Error):
    FILE_NOT_FOUND_ERRORS = [
        r'\[Errno 2\] No such file or directory: \'/home/sandbox/\.ya/tools',
        r'\[Errno 2\] No such file or directory: \'/home/sandbox/\.ya/tmp',
        r'\[Errno 2\] No such file or directory: \'/home/sandbox/\.ya/build',
        r'Error 2: No such file or directory.*/home/sandbox/\.ya/build',
        r'\[Errno 2\] No such file or directory: \'/place/sandbox-data/build_cache/ya/tmp',
        r'\[Errno 2\] No such file or directory: \'/place/sandbox-data/build_cache/ya/tools',
    ]

    ERROR_RE = re.compile('|'.join([e for e in FILE_NOT_FOUND_ERRORS]), re.MULTILINE)
    MESSAGE = 'File not found'


class QuotaExceededError(error_base.Error):
    ERROR_RE = re.compile(r'\[Errno 122\] Quota exceeded')
    MESSAGE = 'Quota exceeded'


class DiskQuotaExceededError(error_base.Error):
    ERROR_RE = re.compile(r'\[Errno 122\] Disk quota exceeded')
    MESSAGE = 'Disk quota exceeded'


class NoSpaceLeftOnDeviceError(error_base.Error):
    ERROR_RE = re.compile(r'No space left on device')
    MESSAGE = 'No space left on device'


class PermissionDeniedError(error_base.Error):
    ERROR_RE = re.compile(r'\[Errno 13\] Permission denied')
    MESSAGE = 'Permission denied'


class AutocheckTokenError(error_base.Error):
    ERROR_RE = re.compile('AutocheckTokenError:')
    MESSAGE = 'Cannot get token from vault'


class YaRunnerLateImportFlakyError(error_base.Error):
    """
    Hopefully, will be fixed in https://st.yandex-team.ru/DEVTOOLSSUPPORT-69240
    """
    ERROR_RE = re.compile(r'AttributeError: module \'yalibrary\.runner\.tasks\' has no attribute')
    MESSAGE = 'Ya runner late import flaky error'


class PythonBuildInErrors(error_base.Error):
    # sorted in the hope that exceptions in first rows occur more often
    PYTHON_BUILTIN_EXCEPTIONS = [
        'Exception:', 'KeyError:', 'ValueError:', 'AttributeError:', 'IndexError:', 'AssertionError:', 'TimeoutError:',
        'FileExistsError:', 'FileNotFoundError:', 'UnicodeError:', 'UnicodeDecodeError:', 'UnicodeEncodeError:', 'RuntimeError:',
        'NotImplementedError:', 'RecursionError:', 'SyntaxError:', 'StopIteration:', 'StopAsyncIteration:', 'ArithmeticError:',
        'FloatingPointError:', 'OverflowError:', 'ZeroDivisionError:', 'BufferError:', 'EOFError:', 'ImportError:',
        'ModuleNotFoundError:', 'LookupError:', 'MemoryError:', 'NameError:', 'UnboundLocalError:', 'BlockingIOError:',
        'ChildProcessError:', 'ConnectionError:', 'BrokenPipeError:', 'ConnectionAbortedError:', 'ConnectionRefusedError:', 'ConnectionResetError:',
        'InterruptedError:', 'IsADirectoryError:', 'NotADirectoryError:', 'PermissionError:', 'ProcessLookupError:',
        'ReferenceError:', 'IndentationError:', 'TabError:', 'SystemError:', 'TypeError:', 'UnicodeTranslateError:',
    ]

    ERROR_RE = re.compile('|'.join(['^' + e for e in PYTHON_BUILTIN_EXCEPTIONS]), re.MULTILINE)
    MESSAGE = 'BuildIn python exception in stderr log'


class YaRuntimeError(error_base.Error):
    ERROR_RE = re.compile(r'RuntimeError: Cannot start process: Process was not created: No such file or directory')
    MESSAGE = 'ya RuntimeError appeared'


class ArcError(error_base.Error):
    ERROR_RE = re.compile(r'Unable to execute arc command')
    MESSAGE = 'Unable to execute arc command'


class PythonSegmentationFaultError(error_base.Error):
    ERROR_RE = re.compile(r'Python error: Segmentation fault')
    MESSAGE = 'Segmentation fault'


class FailedToReadDirDuringConfInitializationError(error_base.Error):
    ERROR_RE = re.compile(r'Conf initialization failed with error: \(Error 5: I\/O error\) .*: failed to readdir')
    MESSAGE = 'Failed to readdir during conf initialization'


class IOErrorFromYmakeConf(error_base.Error):
    ERROR_RE = re.compile(r'Config was not generated due to errors in .*\/ymake_conf.py.*OSError: \[Errno 5\] I\/O error:', re.DOTALL)
    MESSAGE = 'IO error from ymake_conf.py'
