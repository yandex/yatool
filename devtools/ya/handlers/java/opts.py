from devtools.ya.core.yarg import ArgConsumer, ArgsValidatingException, Options, SetConstValueHook, SetValueHook
from devtools.ya.core.yarg.groups import PRINT_CONTROL_GROUP

from . import dep_tree


class DependencyTreeOutputOptions(Options):
    def __init__(self):
        self.dep_tree_format = dep_tree.FORMAT_TEXT
        self.dep_tree_output = None
        self.dep_tree_open = False

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['--format'],
                help='Dependency tree output format',
                hook=SetValueHook('dep_tree_format', values=dep_tree.FORMATS),
                group=PRINT_CONTROL_GROUP,
            ),
            ArgConsumer(
                ['--html'],
                help='Render dependency tree as an html page, same as --format=html',
                hook=SetConstValueHook('dep_tree_format', dep_tree.FORMAT_HTML),
                group=PRINT_CONTROL_GROUP,
            ),
            ArgConsumer(
                ['--output'],
                help='Where to write json or html output (default: {} in the current directory)'.format(
                    ' or '.join(sorted(dep_tree.DEFAULT_OUTPUT.values()))
                ),
                hook=SetValueHook('dep_tree_output'),
                group=PRINT_CONTROL_GROUP,
            ),
            ArgConsumer(
                ['--open'],
                help='Open rendered html page in the browser',
                hook=SetConstValueHook('dep_tree_open', True),
                group=PRINT_CONTROL_GROUP,
            ),
        ]

    def postprocess(self):
        if self.dep_tree_output and self.dep_tree_format == dep_tree.FORMAT_TEXT:
            raise ArgsValidatingException('--output is only supported with --format=json or --format=html')
        if self.dep_tree_open and self.dep_tree_format != dep_tree.FORMAT_HTML:
            raise ArgsValidatingException('--open is only supported with --format=html')
