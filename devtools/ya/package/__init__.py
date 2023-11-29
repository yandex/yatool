class FakeDisplay(object):
    def emit_message(self, msg, verbose=False):
        pass


display = FakeDisplay()

PADDING = '  '
