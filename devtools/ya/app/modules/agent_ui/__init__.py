import logging
import sys

import devtools.ya.core.common_opts as common_opts
from devtools.ya.app.modules import caller_info
from devtools.ya.yalibrary import agent_ui

logger = logging.getLogger(__name__)

# How long configure() waits for the background caller detection before giving
# up on auto-enabling the agent console. Detection usually finishes within
# milliseconds (env-based) and the wait only happens when the option is unset.
DETECTION_TIMEOUT = 1.0


def resolve_agent_output(
    has_option: bool,
    value: str | None,
    caller_info_data: caller_info.CallerInfo | None,
    auto_detect_agent: bool = True,
) -> str | None:
    """Decide where the agent event stream goes.

    Args:
        has_option: Whether the handler exposes the `agent_output` param at all.
        value: The raw --agent-output value (None or '' when not set explicitly).
        caller_info_data: Detected caller info used for auto-enabling, or None.
        auto_detect_agent: The `auto_detect_agent` ya.conf toggle; False
            suppresses auto-enabling only, explicit values still win.

    Returns:
        The effective destination (AgentOutputOptions.STDERR or a file path),
        or None when no agent console should be created. An explicit value
        always wins; AgentOutputOptions.DISABLED suppresses auto-enabling;
        with no explicit value the console is auto-enabled on stderr when a
        coding agent launched ya (only the `agent` field counts).
    """
    if not has_option:
        # The handler doesn't support agent mode: forcing the console would
        # replace the human display with an event stream nobody feeds.
        return None
    if value == common_opts.AgentOutputOptions.DISABLED:
        return None
    if value:
        return value
    if auto_detect_agent and caller_info_data and caller_info_data.get('agent'):
        return common_opts.AgentOutputOptions.STDERR
    return None


def configure(app_ctx):
    has_option = hasattr(app_ctx.params, 'agent_output')
    explicit = getattr(app_ctx.params, 'agent_output', None)
    auto_detect_agent = getattr(app_ctx.params, 'auto_detect_agent', True)
    detected = None
    if has_option and not explicit and auto_detect_agent:
        # Auto-enabling is on the table: block (briefly) for the background
        # detection. A miss or a timeout reads as "not an agent". For
        # sensitive commands the caller_info module is not configured at all,
        # so this returns None and auto-enabling never fires there.
        detected = caller_info.get_caller_info_from_context(app_ctx, timeout=DETECTION_TIMEOUT)
    value = resolve_agent_output(has_option, explicit, detected, auto_detect_agent=auto_detect_agent)
    if value is None:
        yield None
        return

    auto_enabled = not explicit
    _report_enabled(value, auto_enabled)

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


def _report_enabled(destination: str, auto: bool) -> None:
    # Mirrors configure_caller_info's pattern: the module reports its own
    # record through the shared telemetry channel (no-op without backends,
    # e.g. for sensitive commands where the report module is not configured).
    from devtools.ya.core.report import telemetry, ReportTypes

    logger.debug("agent console enabled (destination=%r, auto=%s)", destination, auto)
    telemetry.report(ReportTypes.AGENT_OUTPUT, {'destination': destination, 'auto': auto})
