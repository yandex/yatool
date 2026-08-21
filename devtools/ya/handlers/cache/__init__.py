import devtools.ya.core.yarg as yarg
from .yt import CacheYtHandler


class CacheYaHandler(yarg.CompositeHandler):
    description = 'Maintain remote YT cache'

    def __init__(self):
        yarg.CompositeHandler.__init__(self, description=self.description)
        self['yt'] = CacheYtHandler()
