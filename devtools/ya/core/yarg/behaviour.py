import collections
import logging

from six import iteritems

logger = logging.getLogger(__name__)

NO_DEFAULT_VALUE = object()


class ParamException(Exception):
    pass


class ParamNotFoundException(ParamException):
    pass


class ValidateException(ParamException):
    pass


class UnusedArgumentsException(ParamException):
    pass


class CannotMatchBehaviour(ParamException):
    pass


class Validator(object):
    isint = staticmethod(lambda x: (int(x) == x))


class Param(object):
    def __init__(self, name, default_value=NO_DEFAULT_VALUE, validator=None):
        self.name = name
        self.default_value = default_value
        self.validator = validator

    def fill(self, args, filled_obj):
        if self.default_value != NO_DEFAULT_VALUE:
            value = args.get(self.name, self.default_value)
        else:
            if self.name not in args:
                raise ParamNotFoundException('required name "{}" cannot be found'.format(self.name))
            value = args[self.name]
        if self.validator is not None:
            try:
                if not self.validator(value):
                    raise ValidateException('Cannot validate {} for {}'.format(value, self.name))
            except ValidateException:
                raise
            except Exception as e:
                raise ValidateException(str(e))
        filled_obj[self.name] = value
        return [self.name]


class Expect(object):
    def __init__(self, **kwargs):
        self.kwargs = kwargs

    def fill(self, args, filled_obj):
        for k, v in iteritems(self.kwargs):
            filled_obj[k] = args[k]
        return list(self.kwargs.keys())


class Behaviour(object):
    def __init__(self, action, params=None, expectations=None):
        if params is None:
            params = []
        if expectations is None:
            expectations = []
        self.params = params
        self.expectations = expectations
        self.action = action

    def match(self, args):
        return all(k in args and args[k] == v for exp in self.expectations for k, v in iteritems(exp.kwargs))

    def go(self, args):
        filled_object = {}

        cnt = collections.Counter()
        for param in self.params + self.expectations:
            for x in param.fill(args, filled_object):
                cnt[x] += 1

        unused_params = [x for x in args.keys() if cnt[x] == 0]
        if len(unused_params) > 0:
            # raise UnusedArgumentsException('[{}] arguments are unused'.format(', '.join(unused_params)))
            logger.debug('Found unused args %s', unused_params)  # TODO: switch to exception

        return self.action(**filled_object)


def behave(kwargs, *behaviours):
    assert len(behaviours) > 0

    matched = [x for x in behaviours if x.match(kwargs)]

    if len(matched) != 1:
        raise CannotMatchBehaviour('Not unique matched param bag {}'.format(matched))

    behaviour = matched[0]

    return behaviour.go(kwargs)
