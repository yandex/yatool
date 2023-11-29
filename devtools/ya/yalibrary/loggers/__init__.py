from yalibrary.loggers.display_log import DisplayStreamHandler
from yalibrary.loggers.file_log import TokenFilterFormatter, FILE_LOG_FMT


def filter_logging(replacements):
    import logging

    for handler in logging.root.handlers:
        if isinstance(handler, DisplayStreamHandler):
            logging.debug("Update replacements in %s", type(handler).__name__)
            handler._replacements = replacements
            continue

        if isinstance(handler.formatter, TokenFilterFormatter):
            logging.debug("Update replacements in %s", handler.formatter)
            handler.formatter._replacements = replacements
        else:
            logging.debug("Add suppressing formatter to %s", handler)
            handler.setFormatter(TokenFilterFormatter(FILE_LOG_FMT, replacements))
