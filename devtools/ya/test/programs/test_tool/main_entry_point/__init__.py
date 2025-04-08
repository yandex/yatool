import sys
import argparse

from devtools.ya.yalibrary import app_ctx
from devtools.ya.app import (
    configure_fetcher_params,
    configure_legacy_sandbox_fetcher,
    configure_active_state,
)
from devtools.ya.test.programs.test_tool.lib import run, get_tool_args


class CtxParams:
    def __init__(self, args):
        if args.token_path is not None:
            with open(args.token_path) as f:
                self.oauth_token = f.read().strip()
        else:
            self.oauth_token = args.token


def configure_display():
    from yalibrary import display
    from yalibrary import formatter

    yield display.Display(sys.stderr, formatter.DryFormatter())


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--token", dest="token", help="Path to sandbox token", default=None)
    parser.add_argument("--token-path", dest="token_path", help="Path to sandbox token filepath", default=None)
    args = get_tool_args()
    return parser.parse_known_args(args)


def configure_params(args):
    yield CtxParams(args)


def get_executor():
    args, _ = parse_args()

    # noinspection PyStatementEffect
    def helper():
        ctx = app_ctx.AppCtx()
        sys.modules['app_ctx'] = ctx
        with ctx.configure(
            [
                ('params', configure_params(args)),
                ('display', configure_display()),
                ('state', configure_active_state(ctx)),
                ('fetcher_params', configure_fetcher_params(ctx)),
                ('legacy_sandbox_fetcher', configure_legacy_sandbox_fetcher(ctx)),
            ]
        ):
            return run()

    return helper
