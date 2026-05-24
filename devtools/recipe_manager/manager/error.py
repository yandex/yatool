# A hierarchy of errors that occur when validating user parameters or that result from other detected external factors.
# The key word here is "detected", so no stack trace is output for these errors.


class RecipeError(Exception):
    pass


class InvalidArgumentError(RecipeError):
    pass


class AlreadyExistsError(RecipeError):
    pass


class NotFoundError(RecipeError):
    pass


class PreconditionError(RecipeError):
    pass


class ProcessError(RecipeError):
    pass


class ProcessTimeoutError(ProcessError):
    pass
