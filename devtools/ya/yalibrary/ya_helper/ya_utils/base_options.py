# -*- coding: utf-8 -*-

from __future__ import division, print_function, unicode_literals

from copy import copy
import typing as tp  # noqa: F401

from yalibrary.ya_helper.common import LoggerCounter


class BaseOptions(LoggerCounter):
    """Please, name internal value like `_name` to not to break merge machinery"""

    def __init__(self, **kwargs):
        self._original_kwargs = copy(kwargs)
        self._kwargs = kwargs

        self._logger = self.logger
        del self.logger  # for merge_with machinery

    def _pop(self, key, default=None):
        # type: (str, tp.Any) -> tp.Any
        if key not in self._kwargs:
            return default
        return self._kwargs.pop(key)

    def _check_parameters(self):
        if self._kwargs:
            # XXX: Right now we can't check it in-place
            raise NameError(
                "There is no {} parameters in class {}; check name or add it manually".format(
                    self._kwargs.keys(), type(self).__name__
                )
            )

    def generate(self):
        # type: () -> tp.Tuple[tp.List[str], tp.Dict[str, str]]
        raise NotImplementedError()

    @property
    def _parameters(self):
        # type: () -> tp.Set[str]
        return set([name for name in self.__dict__.keys() if not name.startswith("_")])

    @classmethod
    def _choose_class(cls, other_class):
        """Returns superior class in inheritance tree"""
        if issubclass(cls, other_class):
            return cls

        if issubclass(other_class, cls):
            return other_class._choose_class(cls)

        raise TypeError("{} and {} is not subclasses of each other".format(cls, other_class))

    def merge_with(self, options):
        # type: (BaseOptions, tp.Optional[tp.Type[BaseOptions]]) -> BaseOptions

        if options is None:
            return self

        klass = self._choose_class(type(options))

        _keys = self._parameters

        if not isinstance(options, BaseOptions):
            raise TypeError("Expect YaOptions inheritor, not {}".format(type(options)), options)

        _keys.update(options._parameters)

        keys = tuple(sorted(_keys))

        kwargs = {}

        for key in keys:
            left, right = getattr(self, key, None), getattr(options, key, None)

            if left is None and right is None:
                continue

            if left is not None and right is not None and left != right:
                self._logger.debug("Value for key `%s`: `%s` replaced by `%s`", key, left, right)

            kwargs[key] = left if right is None else right

        return klass._do_merge(self, options, kwargs)

    @classmethod
    def _do_merge(cls, self, options, kwargs):
        return cls(**kwargs)

    def __add__(self, other):
        return self.merge_with(other)

    def __radd__(self, other):
        if not isinstance(other, BaseOptions):
            raise TypeError("Expect {} inheritor, not {}".format(BaseOptions.__name__, type(other)), other)

        return other.merge_with(self)

    @property
    def dict_without_secrets(self):
        return {k: self.__dict__[k] for k in self._parameters if k != "token"}
