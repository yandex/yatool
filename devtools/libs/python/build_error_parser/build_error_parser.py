import six

from . import error_base


def find_error(message: str, errors: list[type[error_base.Error]] | None = None) -> type[error_base.Error] | None:
    message = six.ensure_str(message)
    if not errors:
        errors = error_base.Error.__subclasses__()

    for err in errors:
        if err.is_error_found(message):
            return err

    return None
