from devtools.ya.core.yarg.dispatch import LazyCommand, try_load_handler  # noqa: F401
from devtools.ya.core.yarg.groups import *  # noqa: F401, F403

_LAZY_IMPORTS = {
    # handler
    'BaseHandler': 'devtools.ya.core.yarg.handler',
    'CompositeHandler': 'devtools.ya.core.yarg.handler',
    'FeedbackHandler': 'devtools.ya.core.yarg.handler',
    'OptsHandler': 'devtools.ya.core.yarg.handler',
    'EMPTY_KEY': 'devtools.ya.core.yarg.handler',
    # behaviour
    'behave': 'devtools.ya.core.yarg.behaviour',
    'Behaviour': 'devtools.ya.core.yarg.behaviour',
    'Param': 'devtools.ya.core.yarg.behaviour',
    # config_files
    'load_config': 'devtools.ya.core.yarg.config_files',
    'get_config_files': 'devtools.ya.core.yarg.config_files',
    # consumers
    'return_true_if_enabled': 'devtools.ya.core.yarg.consumers',
    'Compound': 'devtools.ya.core.yarg.consumers',
    'FreeArgConsumer': 'devtools.ya.core.yarg.consumers',
    'SingleFreeArgConsumer': 'devtools.ya.core.yarg.consumers',
    'ArgConsumer': 'devtools.ya.core.yarg.consumers',
    'NullArgConsumer': 'devtools.ya.core.yarg.consumers',
    'EnvConsumer': 'devtools.ya.core.yarg.consumers',
    'ConfigConsumer': 'devtools.ya.core.yarg.consumers',
    # excs
    'BaseOptsFrameworkException': 'devtools.ya.core.yarg.excs',
    'TransformationException': 'devtools.ya.core.yarg.excs',
    'ArgsBindingException': 'devtools.ya.core.yarg.excs',
    'ArgsValidatingException': 'devtools.ya.core.yarg.excs',
    'FlagNotSupportedException': 'devtools.ya.core.yarg.excs',
    'UnsupportedPlatformException': 'devtools.ya.core.yarg.excs',
    # options
    'Options': 'devtools.ya.core.yarg.options',
    'merge_opts': 'devtools.ya.core.yarg.options',
    'RawParamsOptions': 'devtools.ya.core.yarg.options',
    'ParamAsArgs': 'devtools.ya.core.yarg.options',
    # params
    'Params': 'devtools.ya.core.yarg.params',
    'merge_params': 'devtools.ya.core.yarg.params',
    # help_level
    'HelpLevel': 'devtools.ya.core.yarg.help_level',
    # help
    'iterate_options': 'devtools.ya.core.yarg.help',
    'UsageExample': 'devtools.ya.core.yarg.help',
    'ShowHelpOptions': 'devtools.ya.core.yarg.help',
    'ShowHelpException': 'devtools.ya.core.yarg.help',
    # hooks
    'FILES': 'devtools.ya.core.yarg.hooks',
    'BaseHook': 'devtools.ya.core.yarg.hooks',
    'SetValueHook': 'devtools.ya.core.yarg.hooks',
    'SetConstValueHook': 'devtools.ya.core.yarg.hooks',
    'SetAppendHook': 'devtools.ya.core.yarg.hooks',
    'ExtendHook': 'devtools.ya.core.yarg.hooks',
    'DictPutHook': 'devtools.ya.core.yarg.hooks',
    'DictUpdateHook': 'devtools.ya.core.yarg.hooks',
    'SetConstAppendHook': 'devtools.ya.core.yarg.hooks',
    'UpdateValueHook': 'devtools.ya.core.yarg.hooks',
    'CaseInsensitiveValues': 'devtools.ya.core.yarg.hooks',
    'NoValueDummyHook': 'devtools.ya.core.yarg.hooks',
    'SwallowValueDummyHook': 'devtools.ya.core.yarg.hooks',
}


def __getattr__(name):
    if name in _LAZY_IMPORTS:
        import importlib
        module = importlib.import_module(_LAZY_IMPORTS[name])
        value = getattr(module, name)
        globals()[name] = value
        return value
    raise AttributeError("module {!r} has no attribute {!r}".format(__name__, name))
