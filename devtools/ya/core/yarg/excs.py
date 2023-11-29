class BaseOptsFrameworkException(Exception):  # TODO: Rename
    pass


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
