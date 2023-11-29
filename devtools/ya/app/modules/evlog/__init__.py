import os
import datetime
import core.config
import core.gsid
from yalibrary import evlog


def configure(app_ctx):
    for x in evlog.with_evlog(
        app_ctx.params,
        os.path.join(core.config.misc_root(), 'evlogs'),
        10,
        datetime.datetime.now(),
        core.gsid.uid(),
        app_ctx.hide_token2,
    ):
        yield x
