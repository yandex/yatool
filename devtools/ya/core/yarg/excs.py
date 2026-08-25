import devtools.ya.core.error as core_error


class BaseOptsFrameworkException(Exception):  # TODO: Rename
    # A command that failed to parse is a mistake in the command line, not in
    # the code being built. Subclasses are muted, so this is the code
    # configure_exit_code_definition reports for them.
    exit_code = core_error.ExitCodes.USAGE_ERROR


class TransformationException(BaseOptsFrameworkException):
    mute = True


class ArgsBindingException(BaseOptsFrameworkException):
    mute = True


class ArgsValidatingException(BaseOptsFrameworkException):
    mute = True


class FlagNotSupportedException(BaseOptsFrameworkException):
    mute = True


class UnsupportedPlatformException(BaseOptsFrameworkException):
    mute = True
