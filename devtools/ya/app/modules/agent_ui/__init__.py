import sys

import devtools.ya.core.common_opts as common_opts
from devtools.ya.yalibrary import agent_ui


def configure(app_ctx):
    value = getattr(app_ctx.params, 'agent_output', None)
    if not value:
        yield None
        return

    if value == common_opts.AgentOutputOptions.STDERR:
        # stdout is reserved for the handlers' payload (ya dump etc.);
        # the event stream is meta output, like the human display.
        stream = sys.stderr
    else:
        stream = open(value, 'w')
    console = agent_ui.AgentConsole(stream)
    console.start()
    try:
        yield console
    finally:
        console.stop()
        if stream is not sys.stderr:
            stream.close()
