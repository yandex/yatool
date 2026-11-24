import sys

from devtools.ya.core import yarg
from devtools.ya.core.yarg import groups


class LarryOptions(yarg.Options):
    def __init__(self):
        super().__init__()
        self.larry_addr = None
        self.larry_requested_addr = None
        self.larry_local_cache = True
        self.larry_proactive_update_interval = 86400
        # Additional exact names; the adapter retains its built-in defaults.
        self.larry_environment_whitelist = []
        self.larry_test_environment_whitelist = []

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ['--larry-runner'],
                help=(
                    'Use beta Larry (compatible within beta; beta-to-stable protocol may change). '
                    'With YT enabled, explicit proxy/directory/role settings from flags, environment or config '
                    '(including values equal to defaults) require grpc: or grpc-sync:. '
                    'Legacy JSON services also reject explicit credentials; legacy sync: retains token-file forwarding. '
                    'These YT restrictions do not apply with --no-yt-store. '
                    'See devtools/ya/build/larry_client/README.md for YT profile compatibility.'
                ),
                hook=yarg.SetValueHook('larry_addr'),
                group=groups.LARRY_OPT_GROUP,
                visible=yarg.HelpLevel.INTERNAL,
            ),
            yarg.ArgConsumer(
                ['--larry-no-local-cache'],
                help='Omit local artifact caching in Larry; retain configured remote caches',
                hook=yarg.SetConstValueHook('larry_local_cache', False),
                group=groups.LARRY_OPT_GROUP,
                visible=False,
            ),
            yarg.ConfigConsumer('larry_local_cache'),
            yarg.ConfigConsumer('larry_proactive_update_interval'),
            yarg.ConfigConsumer('larry_betta_addr', hook=yarg.SetValueHook('larry_addr')),
            yarg.ConfigConsumer('larry_environment_whitelist'),
            yarg.ConfigConsumer('larry_test_environment_whitelist'),
        ]

    def postprocess(self):
        if type(self.larry_proactive_update_interval) is not int or self.larry_proactive_update_interval < 0:
            raise yarg.ArgsValidatingException('larry_proactive_update_interval must be nonnegative integer seconds')
        for option in ('larry_environment_whitelist', 'larry_test_environment_whitelist'):
            names = getattr(self, option)
            if not isinstance(names, list) or any(
                not isinstance(name, str) or not name or '=' in name or '\x00' in name for name in names
            ):
                raise yarg.ArgsValidatingException(f'{option} must be a list of valid environment names')

    def postprocess2(self, params: yarg.Options) -> None:
        if self.larry_addr is None:
            return

        if sys.platform == 'win32' and self.larry_addr == 'local:':
            self.larry_requested_addr = self.larry_addr
            self.larry_addr = 'sync:'

        # Validate the selected runner before graph construction: otherwise distbuild
        # silently wins dispatch, and local-runner limits can be silently ignored.
        # Keep Larry compatibility here, using yarg's merged, processed options.
        if getattr(params, 'use_distbuild', False):
            raise yarg.ArgsValidatingException('--larry-runner cannot be combined with --dist')

        grpc = self.larry_addr.startswith(('grpc:', 'grpc-sync:')) or (
            sys.platform == 'win32' and self.larry_addr.startswith('sync:')
        )
        if getattr(params, 'yt_store', False) and not grpc:
            # SetValueHook tracks CLI, environment and config assignments even
            # when the explicitly supplied value equals a built-in default.
            yt_overrides = ['yt_proxy', 'yt_dir', 'yt_proxy_role']
            if not self.larry_addr.startswith('sync:'):
                yt_overrides += ['yt_token', 'yt_token_path']
            if any(getattr(params, f'_{name}_counter', 0) for name in yt_overrides):
                raise yarg.ArgsValidatingException('Per-build YT settings require a gRPC Larry address')

        unsupported = (
            ('--lld-threads', getattr(params, 'lld_threads', None) is not None),
            # SetValueHook counts CLI, environment and config assignments, including
            # explicit zero/default values; automatic LTO defaults are not requests.
            ('--link-threads', bool(getattr(params, '_link_threads_counter', 0))),
            ('--memory-limit', getattr(params, 'memory_limit', None) is not None),
            ('--bazel-remote-warming', bool(getattr(params, 'bazel_remote_warming', False))),
            ('--bazel-remote-warming-manifest', getattr(params, 'bazel_remote_warming_manifest', None) is not None),
            ('--bazel-remote-put', not getattr(params, 'bazel_remote_readonly', True)),
        )
        for option, requested in unsupported:
            if requested:
                raise yarg.ArgsValidatingException(f'{option} is not supported with Larry')
