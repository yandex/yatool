from devtools.ya.core import yarg
from devtools.ya.core.yarg import groups


class LarryOptions(yarg.Options):
    def __init__(self):
        super().__init__()
        self.larry_addr = None
        # Additional exact names; the adapter retains its built-in defaults.
        self.larry_environment_whitelist = []
        self.larry_test_environment_whitelist = []

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ['--larry-runner'],
                help='Use experimental LARRY server to execute the build',
                hook=yarg.SetValueHook('larry_addr'),
                group=groups.LARRY_OPT_GROUP,
                visible=False,
            ),
            yarg.EnvConsumer('YA_LARRY_ADDR', hook=yarg.SetValueHook('larry_addr')),
            yarg.ConfigConsumer('larry_addr'),
            yarg.ConfigConsumer('larry_environment_whitelist'),
            yarg.ConfigConsumer('larry_test_environment_whitelist'),
        ]

    def postprocess(self):
        for option in ('larry_environment_whitelist', 'larry_test_environment_whitelist'):
            names = getattr(self, option)
            if not isinstance(names, list) or any(
                not isinstance(name, str) or not name or '=' in name or '\x00' in name for name in names
            ):
                raise yarg.ArgsValidatingException(f'{option} must be a list of valid environment names')

    def postprocess2(self, params: yarg.Options) -> None:
        if self.larry_addr is None:
            return

        # Validate the selected runner before graph construction: otherwise distbuild
        # silently wins dispatch, and local-runner limits can be silently ignored.
        # Keep Larry compatibility here, using yarg's merged, processed options.
        if getattr(params, 'use_distbuild', False):
            raise yarg.ArgsValidatingException('--larry-runner cannot be combined with --dist')

        unsupported = (
            ('--lld-threads', getattr(params, 'lld_threads', None) is not None),
            # SetValueHook counts CLI, environment and config assignments, including
            # explicit zero/default values; automatic LTO defaults are not requests.
            ('--link-threads', bool(getattr(params, '_link_threads_counter', 0))),
            ('--memory-limit', getattr(params, 'memory_limit', None) is not None),
            ('--bazel-remote-warming', bool(getattr(params, 'bazel_remote_warming', False))),
            ('--bazel-remote-warming-manifest', getattr(params, 'bazel_remote_warming_manifest', None) is not None),
        )
        for option, requested in unsupported:
            if requested:
                raise yarg.ArgsValidatingException(f'{option} is not supported with Larry')
