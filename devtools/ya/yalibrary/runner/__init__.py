import yalibrary.worker_threads as worker_threads  # noqa


class ExpectedNodeException(Exception):
    def __init__(self, exit_code, message):
        self.exit_code = exit_code
        self.args = (message,)


def called_by_agent(app_ctx) -> bool:
    """Tell whether ya was launched by a coding agent.

    Reads the caller_info app module through getattr (yalibrary must not
    import app.modules): the provider detects the caller in a background
    thread, so an unfinished detection reads as "not an agent".
    """
    provider = getattr(app_ctx, 'caller_info', None)
    if provider is None:
        return False
    data = provider.get_nowait() if hasattr(provider, 'get_nowait') else provider
    try:
        return bool(data and data.get('agent'))
    except Exception:
        return False
