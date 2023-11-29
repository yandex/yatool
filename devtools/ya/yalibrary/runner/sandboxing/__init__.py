# flake8: noqa
try:
    from .yandex_sandboxing import *
except ImportError:
    from .opensource_sandboxing import *
