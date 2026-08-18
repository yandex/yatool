import sys

import devtools.ya.core.common_opts as common_opts
from yalibrary import agent_ui


def configure(app_ctx):
    value = getattr(app_ctx.params, 'agent_output', None)
    if not value:
        yield None
        return

    opened_file = None
    if value == common_opts.AgentOutputOptions.STDERR:
        # stdout is reserved for the handlers' payload (ya dump etc.);
        # the event stream is meta output, like the human display.
        stream = sys.stderr
    else:
        stream = opened_file = open(value, 'w')
    console = agent_ui.AgentConsole(stream)
    console.start()
    try:
        yield console
    finally:
        console.stop()
        if opened_file is not None:
            opened_file.close()
