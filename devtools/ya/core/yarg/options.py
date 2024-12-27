import os
import six
import typing as tp  # noqa: F401

from devtools.ya.core.yarg.config_files import get_config_files
from devtools.ya.core.yarg.consumers import Consumer  # noqa: F401
from devtools.ya.core.yarg.consumers import Compound, ArgConsumer, FreeArgConsumer, get_consumer
from devtools.ya.core.yarg.help_level import HelpLevel
from devtools.ya.core.yarg.hooks import SetRawParamsHook, ExtendHook, SetRawParamsFileHook
from devtools.ya.core.yarg.groups import OPERATIONAL_CONTROL_GROUP
from devtools.ya.core.yarg.params import Params, merge_params


class Options(object):
    visible = None
    _param_as_args = False

    def __init__(self, visible=None):
        # type: (HelpLevel) -> None
        # XXX: To use this, add super call to inheritor
        self.visible = visible

    def params(self):
        return Params(
            **dict(
                (
                    k,
                    v,
                )
                for k, v in six.iteritems(self.__dict__)
                if not k.startswith('_')
            )
        )

    def consumer(self):
        return Consumer()

    def postprocess(self):
        pass

    def postprocess2(self, params):  # TODO: Rename to opt
        # type: (Options) -> None
        pass

    def initialize(self, args, prefix=None, unknown_args_as_free=False, global_config=True, user_config=True):
        return self.params()


class _MergedOptions(Options):
    def __init__(self, opts):
        self._opts = []
        self._param_as_args = False
        self._postprocessed = False

        for opt in opts:
            self.append(opt)

    def __getattribute__(self, item):
        try:
            return super(_MergedOptions, self).__getattribute__(item)
        except AttributeError:
            for opt in self._opts:
                if hasattr(opt, item):
                    return getattr(opt, item)
            raise

    def __iter__(self):
        for x in self._opts:
            yield x

    def __setattr__(self, key, value):
        self.__dict__[key] = value  # XXX: upyacha
        for opt in self._opts:
            if hasattr(opt, key):
                setattr(opt, key, value)

    def consumer(self):
        lst = [get_consumer(x) for x in self._opts]
        return Compound(*lst)

    def postprocess(self):
        if self._postprocessed:
            return
        for opt in self._opts:
            opt.postprocess()
        self._postprocessed = True

    def postprocess2(self, params):
        self.postprocess()
        for opt in self._opts:
            opt.postprocess2(params)

    def params(self):
        # XXX: call postprocess2
        self.postprocess()

        return merge_params(*[x.params() for x in self._opts])

    def initialize(self, args, prefix=None, unknown_args_as_free=False, global_config=True, user_config=True):
        from devtools.ya.core.yarg.populate import populate  # to avoid circular import options <-> populate

        config_files = get_config_files(
            cmd_name='_'.join(prefix[1:]) if prefix else None, global_config=global_config, user_config=user_config
        )
        populate(
            self,
            args,
            env=os.environ,
            unknown_args_as_free=unknown_args_as_free,
            config_files=config_files,
            prefix=prefix,
        )
        return self.params()

    def append(self, opt):
        # type: (Options) -> None
        if self._postprocessed:
            raise RuntimeError("Can't add options because {} already postprocessed".format(repr(self)))

        if not isinstance(opt, Options):
            raise TypeError("Expect subclass of {}, not {}".format(Options.__name__, type(opt).__name__))

        if isinstance(opt, _MergedOptions):
            for opt in self._opts:
                self.append(opt)
        elif isinstance(opt, ParamAsArgs):
            self._param_as_args = True
            self._opts.append(opt)
        elif isinstance(opt, Options):
            self._opts.append(opt)
        else:
            raise TypeError("Wrong Options type: {}".format(type(opt)))

        if self._param_as_args and len(self._opts) > 1:
            raise TypeError("You can use only one ParamAsArgs in _MergedOptions, can't add {}".format(type(opt)))

    def __str__(self):
        return 'merged({})'.format(', '.join([type(x).__name__ for x in self._opts]))


class RawParamsOptions(Options):
    def __init__(self):
        self.raw_params = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--raw-params'],
                help='Params dict as json encoded with base64',
                hook=SetRawParamsHook('raw_params'),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
            ArgConsumer(
                ['--raw-params-file'],
                help='Params dict stored in .json-format file',
                hook=SetRawParamsFileHook('raw_params'),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.INTERNAL,
            ),
        ]


class ParamAsArgs(Options):
    _params_as_args = True

    def __init__(self):
        self.args = []

    def params(self):
        return self.args

    @staticmethod
    def consumer():
        return FreeArgConsumer(help='args', hook=ExtendHook('args'))


def merge_opts(opts):
    # type: (tp.Sequence[Options]) -> Options
    return _MergedOptions(opts)
