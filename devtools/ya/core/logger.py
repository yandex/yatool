import logging


VERBOSE_LEVEL = logging.INFO


def level():
    global VERBOSE_LEVEL
    return VERBOSE_LEVEL


def init_logger(verbose_level):
    global VERBOSE_LEVEL
    VERBOSE_LEVEL = verbose_level
