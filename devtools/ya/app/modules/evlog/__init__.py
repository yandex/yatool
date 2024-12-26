import devtools.ya.core.config
from yalibrary import evlog


def configure(app_ctx):
    for x in evlog.with_evlog(
        app_ctx.params,
        devtools.ya.core.config.evlogs_root(),
        app_ctx.hide_token2,
    ):
        yield x
