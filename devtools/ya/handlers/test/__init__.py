import app

from build.build_handler import do_ya_make
from build.build_opts import ya_make_options

import core.yarg


class YaTestYaHandler(core.yarg.OptsHandler):
    description = 'Build and run all tests\n[[imp]]ya test[[rst]] is an alias for [[imp]]ya make -A[[rst]]'

    def __init__(self):
        core.yarg.OptsHandler.__init__(
            self,
            action=app.execute(action=do_ya_make),
            examples=[
                core.yarg.UsageExample(
                    '{prefix}',
                    'Build and run all tests',
                ),
                core.yarg.UsageExample(
                    '{prefix} -t',
                    'Build and run small tests only',
                ),
                core.yarg.UsageExample(
                    '{prefix} -tt',
                    'Build and run small and medium tests',
                ),
                core.yarg.UsageExample(
                    '{prefix} -L',
                    'Print test names, don\'t run them',
                ),
                core.yarg.UsageExample(
                    '{prefix} -F "*subname*"',
                    'Build and run test which name contains "subname"',
                ),
            ],
            description=self.description,
            opts=ya_make_options(
                free_build_targets=True,
                is_ya_test=True,
                strip_idle_build_results=True,
            ),
            visible=True,
        )
