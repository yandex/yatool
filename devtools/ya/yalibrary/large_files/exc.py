class ExternalFileException(Exception):
    def __init__(self, message, path=None):
        self.message = message
        self.path = path

    @property
    def args(self):
        return self.message, self.path

    def __repr__(self):
        return "<ExternalFileException: `{}` {}>".format(
            self.path,
            self.message
        )
