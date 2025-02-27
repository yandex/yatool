import devtools.ya.core.sec as sec

from yalibrary import loggers
from devtools.ya.core.report import set_suppression_filter


def configure(app_ctx):
    replacements = sec.mine_suppression_filter(app_ctx.params.__dict__)
    loggers.filter_logging(replacements)
    set_suppression_filter(replacements)

    yield replacements
