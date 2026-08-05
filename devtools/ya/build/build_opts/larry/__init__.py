from devtools.ya.core import yarg
from devtools.ya.core.yarg import groups


class LarryOptions(yarg.Options):
    def __init__(self):
        super().__init__()
        self.larry_addr = None

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
        ]
