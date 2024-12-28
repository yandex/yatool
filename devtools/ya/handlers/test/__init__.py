import devtools.ya.app

from devtools.ya.build.build_handler import do_ya_make
from devtools.ya.build.build_opts import ya_make_options

import devtools.ya.core.yarg


class YaTestYaHandler(devtools.ya.core.yarg.OptsHandler):
    description = 'Build and run all tests\n[[imp]]ya test[[rst]] is alias for [[imp]]ya make -A[[rst]]'

    extra_help = (
        "By default, [[imp]]ya test[[rst]] [[alt1]]doesn't build targets that tests do not need[[rst]].\n"
        "To build all targets use [[imp]]-b[[rst]] / [[imp]]--build-all[[rst]] option."
    )

    def __init__(self):
        devtools.ya.core.yarg.OptsHandler.__init__(
            self,
            action=devtools.ya.app.execute(action=do_ya_make),
            examples=[
                devtools.ya.core.yarg.UsageExample(
                    '{prefix}',
                    'Build and run all tests',
                ),
                devtools.ya.core.yarg.UsageExample(
                    '{prefix} -t',
                    'Build and run small tests only',
                ),
                devtools.ya.core.yarg.UsageExample(
                    '{prefix} -tt',
                    'Build and run small and medium tests',
                ),
                devtools.ya.core.yarg.UsageExample(
                    '{prefix} -L',
                    'Print test names, don\'t run them',
                ),
                devtools.ya.core.yarg.UsageExample(
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
            extra_help=self.extra_help,
        )
