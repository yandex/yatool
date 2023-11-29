import logging


class DisplayStreamHandler(logging.StreamHandler):
    level_map = {
        logging.DEBUG:       ('Debug', '[[unimp]]'),
        logging.INFO:        ('Info',  '[[good]]'),
        logging.WARNING:     ('Warn',  '[[warn]]'),
        logging.ERROR:       ('Error', '[[bad]]'),
        logging.CRITICAL:    ('Fatal', '[[bad]]'),
    }

    def __init__(self, display, replacements):
        super(DisplayStreamHandler, self).__init__()
        self._display = display
        self._replacements = replacements

    def _filter(self, s):
        for r in self._replacements:
            s = s.replace(r, "[SECRET]")
        return s

    def gen_prefix(self, level_name):
        prefix, fg = self.level_map[level_name]
        return fg + prefix + '[[rst]]: '

    def format(self, record):
        message = logging.StreamHandler.format(self, record)
        prefix = self.gen_prefix(record.levelno)
        return prefix + message

    def emit(self, record):
        try:
            self._display.emit_message(self._filter(self.format(record)))
        except (KeyboardInterrupt, SystemExit):
            raise
        except Exception:
            self.handleError(record)


def with_display_log(app_ctx, level, replacements):
    root = logging.getLogger()
    root.setLevel(logging.DEBUG)

    display_handler = DisplayStreamHandler(app_ctx.display, replacements)
    display_handler.setLevel(level)

    if hasattr(app_ctx, 'display_in_memory_log'):
        handler = app_ctx.display_in_memory_log
        for log_entry in handler.storage:
            display_handler.emit(log_entry)

        handler.close()
        root.removeHandler(handler)

    root.addHandler(display_handler)

    yield
