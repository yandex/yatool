import binascii
import typing as tp  # noqa: F401
import six

import base64
import logging
from devtools.ya.core.yarg.excs import TransformationException, ArgsValidatingException
from exts import yjson as json
from exts import compatible23  # noqa


logger = logging.getLogger(__name__)


if tp.TYPE_CHECKING:
    from devtools.ya.core.yarg.options import Options  # noqa: F401


# used for autocompletion
# see https://zsh.sourceforge.io/Doc/Release/Completion-System.html#index-_005ffiles
FILES = ['_files']


def counter_name(name):
    return '_{}_counter'.format(name)


class CaseInsensitiveValues(list):
    def __init__(self, seq=()):
        assert all(isinstance(val, str) for val in seq), "all values should be str in %s" % seq
        super(CaseInsensitiveValues, self).__init__(seq)

    def __contains__(self, item):
        assert isinstance(item, str), "%s should be string" % item
        return any(item.lower() == val.lower() for val in self)


class BaseHook(object):
    def __call__(self, to, *args):
        # type: ("Options", tp.Optional[tp.Any]) -> None
        raise NotImplementedError()

    @staticmethod
    def need_value():
        """How much value need to be passed into __call__
        True: Options + Any
        False: Options
        """
        return True

    def default(self, to):
        # type: ("Options") -> tp.Optional[str]
        """Used for display default value in help"""
        return None

    def available_options(self, to):
        # type: ("Options") -> tp.Optional[str]
        """Used for display available options in help"""
        return None


class ValueHook(BaseHook):
    def __init__(self, values=None, *args, **kwargs):
        # type: (tp.Optional[tp.Union[tp.Callable, tp.Iterable[str]]], tp.Any, tp.Any) -> None
        self._values = values or FILES

    @property
    def values(self):
        if callable(self._values):
            self._values = self._values()
        return self._values


class SetValueHook(ValueHook):
    def __init__(self, name, transform=None, values=FILES, default_value=None, values_limit=None):
        # type: (str, tp.Optional[tp.Callable[[tp.Any], tp.Any]], tp.Iterable[tp.Any], tp.Any) -> None
        super(SetValueHook, self).__init__(values)
        self.name = name
        self.transform = transform
        self.default_value = default_value
        self.values_limit = values_limit

    def __call__(self, to, x):
        if not hasattr(to, self.name):
            raise Exception("{0} doesn't have {1} attr".format(to, self.name))
        try:
            value = x if self.transform is None else self.transform(x)
        except Exception as e:
            raise TransformationException(str(e))

        if self.values != FILES and value not in self.values:
            raise ArgsValidatingException(
                "Invalid choice ({}): {} (choose from '{}')".format(self.name, value, "', '".join(self.values))
            )

        setattr(to, self.name, value)

        c_name = counter_name(self.name)
        count = getattr(to, c_name, 0)
        if self.values_limit and count >= self.values_limit:
            raise ArgsValidatingException(
                "You can pass '{}' option only {} time(s)".format(self.name, self.values_limit)
            )
        setattr(to, c_name, count + 1)

    def default(self, to):
        if hasattr(to, self.name):
            current_value = getattr(to, self.name)
            if self.default_value:
                default = self.default_value(current_value)
            elif current_value is not None:
                default = current_value
            else:
                return
            return "(default: '{}')".format(default)
        else:
            return None

    def available_options(self, to):
        if hasattr(to, self.name) and self.values is not FILES:
            return "(available options: '{}')".format("', '".join(self.values))
        else:
            return None

    @staticmethod
    def need_value():
        return True


class SetConstValueHook(BaseHook):
    def __init__(self, name, const):
        self.name = name
        self.const = const

    def __call__(self, to):
        if not hasattr(to, self.name):
            raise Exception("{0} doesn't have {1} attr".format(to, self.name))
        setattr(to, self.name, self.const)

        c_name = counter_name(self.name)
        setattr(to, c_name, getattr(to, c_name, 0) + 1)

    @staticmethod
    def need_value():
        return False


class SetAppendHook(ValueHook):
    def __init__(self, name, transform=None, values=FILES):
        super(SetAppendHook, self).__init__(values)
        self.name = name
        self.transform = transform

    def __call__(self, to, x):
        if not hasattr(to, self.name):
            raise Exception("{0} doesn't have {1} attr".format(to, self.name))
        lst = getattr(to, self.name)
        try:
            lst.append(x if self.transform is None else self.transform(x))
        except Exception as e:
            raise TransformationException(str(e))

        self._validate(lst)

        setattr(to, self.name, lst)

    def _validate(self, lst):
        if self.values != FILES and any(value not in self.values for value in lst):
            raise ArgsValidatingException(
                "Invalid choice ({}): {} (choose from '{}')".format(
                    self.name, str([value for value in lst if value not in self.values]), "', '".join(self.values)
                )
            )

    @staticmethod
    def need_value():
        return True


class ExtendHook(ValueHook):
    def __init__(self, name, transform=None, values=FILES):
        super(ExtendHook, self).__init__(values)
        self.name = name
        self.transform = transform

    def __call__(self, to, x):
        if not hasattr(to, self.name):
            raise Exception("{0} doesn't have {1} attr".format(to, self.name))
        lst = getattr(to, self.name)
        try:
            lst.extend(x if self.transform is None else self.transform(x))
        except Exception as e:
            raise TransformationException(str(e))

        if self.values is not None and self.values != FILES:
            bad_values = [value for value in lst if value not in self.values]
            if bad_values:
                raise ArgsValidatingException(
                    "Invalid choice ({}): {} (choose from '{}')".format(
                        self.name,
                        str(bad_values),
                        "', '".join(self.values),
                    )
                )

        setattr(to, self.name, lst)

    @staticmethod
    def need_value():
        return True


class DictPutHook(ValueHook):
    def __init__(self, name, default_value=None, values=FILES):
        super(DictPutHook, self).__init__(values)
        self.name = name
        self.default_value = default_value

    def __call__(self, to, x):
        if not hasattr(to, self.name):
            raise Exception("{0} doesn't have {1} attr".format(to, self.name))
        dct = getattr(to, self.name)
        key, value = (x.split('=', 1) + [self.default_value])[:2]

        if self.values != FILES and value not in self.values:
            raise ArgsValidatingException(
                "Invalid choice ({}): {} (choose from '{}')".format(self.name, value, "', '".join(self.values))
            )

        dct[key] = value
        setattr(to, self.name, dct)

    @staticmethod
    def need_value():
        return True


class SetConstAppendHook(BaseHook):
    def __init__(self, name, const):
        self.name = name
        self.const = const

    def __call__(self, to):
        if not hasattr(to, self.name):
            raise Exception("{0} doesn't have {1} attr".format(to, self.name))
        lst = getattr(to, self.name)
        if isinstance(self.const, list):
            lst.extend(self.const)
        else:
            lst.append(self.const)
        setattr(to, self.name, lst)

    @staticmethod
    def need_value():
        return False


class SetRawParamsHook(ValueHook):
    def __init__(self, name, values=FILES):
        super(SetRawParamsHook, self).__init__(values)
        self.name = name

    def _load_params(self, x):
        return json.loads(base64.decodebytes(six.ensure_binary(x)))

    def __call__(self, obj, x):
        try:
            params_dict = self._load_params(x)
        except (binascii.Error, ValueError, TypeError):
            raise ArgsValidatingException('Raw params value must be base64(json)')

        for name, value in six.iteritems(params_dict):
            if self.values != FILES and value not in self.values:
                raise ArgsValidatingException(
                    "Invalid choice ({}): {} (choose from '{}')".format(self.name, value, "', '".join(self.values))
                )
            setattr(obj, name, value)

    @staticmethod
    def default(_):
        return ''

    @staticmethod
    def need_value():
        return True


class SetRawParamsFileHook(SetRawParamsHook):
    def _load_params(self, x):
        with open(six.ensure_str(x)) as file:
            return json.load(file)


class UpdateValueHook(BaseHook):
    def __init__(self, name, updater):
        self.name = name
        self.updater = updater

    def __call__(self, to):
        if not hasattr(to, self.name):
            raise Exception("{0} doesn't have {1} attr".format(to, self.name))
        value = getattr(to, self.name)
        value = self.updater(value)
        setattr(to, self.name, value)

    @staticmethod
    def need_value():
        return False


class DictUpdateHook(BaseHook):
    def __init__(self, name):
        self.name = name

    def __call__(self, to, value):
        if not hasattr(to, self.name):
            raise Exception("{0} doesn't have {1} attr".format(to, self.name))

        orig_value = getattr(to, self.name)
        if not isinstance(orig_value, dict):
            raise Exception("Value in {} attr is not dict (`{}`)".format(self.name, type(orig_value)))

        if isinstance(value, list):
            for item in value:
                orig_value.update(item)
        elif isinstance(value, dict):
            orig_value.update(value)
        else:
            raise NotImplementedError(
                "Unknown type to update attribute `{}`: {}, expect dict".format(self.name, type(value))
            )


class NoValueDummyHook(BaseHook):
    def __call__(self, to, *args):
        # type: ("Options", tp.Optional[tp.Any]) -> None
        pass

    @staticmethod
    def need_value():
        return False


class SwallowValueDummyHook(BaseHook):
    """
    Same as NoValueDummyHook, but mandates parameter value to be swallowed
    """

    def __call__(self, to, *args):
        # type: ("Options", tp.Optional[tp.Any]) -> None
        pass

    @staticmethod
    def need_value():
        return True
