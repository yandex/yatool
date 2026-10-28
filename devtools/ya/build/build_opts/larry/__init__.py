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
