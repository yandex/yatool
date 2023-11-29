import six

SOURCE_ROOT = '$(SOURCE_ROOT)'
BUILD_ROOT = '$(BUILD_ROOT)'


def resolve_graph_value(x, upper=True, **kwargs):
    for key, value in six.iteritems(kwargs):
        x = x.replace('$({})'.format(key.upper() if upper else key), value)
    return x


class GraphPath(object):
    def __init__(self, path):
        self.path = path

    @property
    def source(self):
        return self.path.startswith(SOURCE_ROOT)

    @property
    def build(self):
        return self.path.startswith(BUILD_ROOT)

    def resolve(self, **kwargs):
        return resolve_graph_value(self.path, **kwargs)

    def strip(self):
        path = self.resolve(source_root='', build_root='')
        if path.startswith('/'):
            path = path[1:]
        return path
