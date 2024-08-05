import sys

from yalibrary import app_ctx


def configure_display():
    from yalibrary import display
    from yalibrary import formatter

    yield display.Display(sys.stderr, formatter.DryFormatter())


def get_executor():
    from devtools.ya.test.programs.test_tool.lib import run

    # noinspection PyStatementEffect
    def helper():
        ctx = app_ctx.AppCtx()
        sys.modules['app_ctx'] = ctx
        with ctx.configure(
            [
                ('display', configure_display()),
            ]
        ):
            return run()

    return helper
