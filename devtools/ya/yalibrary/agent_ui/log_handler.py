"""Root-logger handler that forwards warnings into the agent stream.

Modeled on DisplayStreamHandler (yalibrary/loggers/display_log): the same
secret masking and the early-buffer replay, but the record is projected
into a structured message event instead of a markup string for a display.
"""

import logging

from yalibrary.agent_ui import projection
from yalibrary.loggers.file_log import TokenFilterFormatter


class AgentLogHandler(logging.Handler):
    def __init__(self, console, replacements):
        super().__init__()
        self._console = console
        self._seen: set = set()
        # Must be a TokenFilterFormatter: filter_logging (yalibrary/loggers)
        # force-replaces any other formatter with the file-log one, whose
        # timestamp prefix is noise for an agent and breaks the dedup below.
        # It also masks the secrets and keeps their list fresh.
        self.setFormatter(TokenFilterFormatter('%(message)s', list(replacements or [])))

    def filter(self, record) -> bool:
        # The level gate lives here to keep the handler self-contained:
        # Handler.handle and the buffer replay do not check it themselves.
        if record.levelno < self.level:
            return False
        # Reentrancy guard: the console logs its own write failures, so
        # forwarding its records would loop the failure back into itself.
        if record.name.startswith('yalibrary.agent_ui'):
            return False
        return super().filter(record)

    def emit(self, record) -> None:
        try:
            event = self._project(record)
            if event is None:
                return
            # Per-node warnings repeat en masse; one copy is enough.
            key = (event['severity'], event['text'])
            if key in self._seen:
                return
            self._seen.add(key)
            self._console.emit(event)
        except (KeyboardInterrupt, SystemExit):
            raise
        except Exception:
            self.handleError(record)

    def _project(self, record) -> dict | None:
        # The formatter masks the secrets, see __init__.
        text = projection.plain_message_text(self.format(record))
        if not text:
            return None
        return {'type': 'message', 'severity': self._severity(record.levelno), 'text': text}

    @staticmethod
    def _severity(levelno: int) -> str:
        if levelno >= logging.CRITICAL:
            return 'fatal'
        if levelno >= logging.ERROR:
            return 'error'
        return 'warning'


def with_agent_log(app_ctx, console) -> None:
    """Attach the agent log handler to the root logger.

    Must run before with_display_log: that one replays and closes the
    display_in_memory_log early buffer.
    """
    handler = AgentLogHandler(console, app_ctx.hide_token)
    handler.setLevel(logging.WARNING)

    in_memory = getattr(app_ctx, 'display_in_memory_log', None)
    if in_memory is not None:
        for record in in_memory.storage:
            # Replay bypasses the logging machinery; the handler's filter
            # applies all its gates, including the level one.
            handler.handle(record)

    logging.getLogger().addHandler(handler)
