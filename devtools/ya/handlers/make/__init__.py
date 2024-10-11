from __future__ import absolute_import
from build.build_handler import do_ya_make
from build.build_opts import ya_make_options

import core.yarg

import devtools.ya.app


class MakeYaHandler(core.yarg.OptsHandler):
    description = 'Build and run tests\nTo see more help use [[imp]]-hh[[rst]]/[[imp]]-hhh[[rst]]'
    stderr_help = '[[alt1]]To see more help use [[imp]]-hh[[rst]]/[[imp]]-hhh[[rst]]'

    def __init__(self):
        core.yarg.OptsHandler.__init__(
            self,
            action=devtools.ya.app.execute(action=do_ya_make),
            examples=[
                core.yarg.UsageExample('{prefix} -r', 'Build current directory in release mode'),
                core.yarg.UsageExample('{prefix} -t -j16 library', 'Build and test library with 16 threads'),
                core.yarg.UsageExample('{prefix} --checkout -j0', 'Checkout absent directories without build'),
            ],
            stderr_help=self.stderr_help,
            description=self.description,
            opts=ya_make_options(
                free_build_targets=True,
                strip_idle_build_results=True,
            ),
            visible=True,
        )
