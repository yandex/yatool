import devtools.ya.core.yarg as yarg
from .yt import CacheYtHandler


class CacheYaHandler(yarg.CompositeHandler):
    description = 'Cache maintenance'

    def __init__(self):
        yarg.CompositeHandler.__init__(self, description=self.description)
        self['yt'] = CacheYtHandler()
