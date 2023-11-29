import logging


def fix_logging():
    class NotRequestsLoggingFilter(logging.Filter):
        def filter(self, record):
            if record.name.startswith("requests") or record.name == "sandbox.common.rest":
                return False
            if record.name == "sandbox.common.upload" and record.levelno < logging.ERROR:
                return False
            return True

    # hide all requests logging lower than ERROR to console
    for h in logging.getLogger().handlers:
        if not isinstance(h, logging.FileHandler):
            h.addFilter(NotRequestsLoggingFilter())
