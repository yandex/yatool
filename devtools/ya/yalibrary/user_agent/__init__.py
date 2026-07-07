import logging
import os
import socket

logger = logging.getLogger(__name__)


def get_current_handler():
    # type: () -> str
    """Returns current ya handler like 'make' or 'ide-vscode', empty string if unknown."""
    try:
        import app_ctx

        # prefix is like ['ya', 'make'] or ['ya', 'ide', 'vscode'], see OptsHandler.handle
        prefix = app_ctx.handler_info["handler"]["prefix"]
        return "-".join(prefix[1:])
    except (ImportError, AttributeError, KeyError):
        return ""


def make_user_agent(prefix="ya"):
    # type: (str) -> str
    """Hostname-based User-Agent enriched with the current invocation context."""
    user_agent = "{}: {}".format(prefix, socket.gethostname())
    handler = get_current_handler()
    if handler:
        user_agent += " handler={}".format(handler)
    distbuild_task_uid = os.getenv("DISTBUILD_TASK_UID")
    if distbuild_task_uid:
        user_agent += " task_uid={}".format(distbuild_task_uid)
    logger.debug("User agent: %s", user_agent)
    return user_agent
