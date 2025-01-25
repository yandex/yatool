class ConfigureError(Exception):
    mute = True


class PathConfigureError(object):
    def __init__(self):
        self.missing_inputs = []

    def __str__(self):
        s = []
        s.extend('missing input ' + x for x in self.missing_inputs)

        return '\n'.join(s)

    def get_colored_errors(self):
        for x in self.missing_inputs:
            yield 'Trying to set a [[alt1]]JAVA_SRCS[[rst]] for a missing directory: [[imp]]{}[[rst]]'.format(
                x
            ), '-WBadDir'
