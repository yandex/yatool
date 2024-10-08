import sys

app_ctx = sys.modules.get('app_ctx')


def stop() -> None:
    if app_ctx:
        app_ctx.state.stop()


def check_cancel_state() -> None:
    if app_ctx:
        app_ctx.state.check_cancel_state()
