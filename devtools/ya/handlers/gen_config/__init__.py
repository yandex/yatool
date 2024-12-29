import devtools.ya.core.common_opts
import devtools.ya.core.yarg

from . import gen_config


class GenConfigOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.output = None
        self.dump_defaults = False

    @staticmethod
    def consumer():
        return [
            devtools.ya.core.yarg.SingleFreeArgConsumer(
                help='ya.conf',
                hook=devtools.ya.core.yarg.SetValueHook('output'),
                required=False,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--dump-defaults'],
                help='Dump default values as JSON',
                hook=devtools.ya.core.yarg.SetConstValueHook('dump_defaults', True),
            ),
        ]


class GenConfigYaHandler(devtools.ya.core.yarg.OptsHandler):
    description = 'Generate default ya config'

    def __init__(self):
        self._root_handler = None
        super().__init__(
            action=self.do_generate,
            description=self.description,
            opts=[
                devtools.ya.core.common_opts.ShowHelpOptions(),
                GenConfigOptions(),
            ],
        )

    def handle(self, root_handler, args, prefix):
        self._root_handler = root_handler
        super().handle(root_handler, args, prefix)

    def do_generate(self, args):
        gen_config.generate_config(self._root_handler, args.output, args.dump_defaults)
