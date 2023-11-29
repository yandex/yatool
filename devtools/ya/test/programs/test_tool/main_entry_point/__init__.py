import sys

from yalibrary import app_ctx


def configure_display():
    from yalibrary import display
    from yalibrary import formatter

    yield display.Display(sys.stderr, formatter.DryFormatter())


def configure_fetchers_storage():
    import yalibrary.fetcher.fetchers_storage as fetchers_storage

    storage = fetchers_storage.FetchersStorage()
    try:
        from yalibrary.yandex.sandbox import fetcher

        storage.register(['sbr', 'http', 'https'], fetcher.SandboxFetcher())
    except (ImportError, AttributeError):
        pass

    yield storage


def get_executor():
    from devtools.ya.test.programs.test_tool.lib import run

    # noinspection PyStatementEffect
    def helper():
        ctx = app_ctx.AppCtx()
        sys.modules['app_ctx'] = ctx
        with ctx.configure(
            [
                ('display', configure_display()),
                ('fetchers_storage', configure_fetchers_storage()),
            ]
        ):
            return run()

    return helper
