import exts.func
from core.yarg.handler import BaseHandler


def try_load_handler(loader):
    module = loader()
    symbols = dir(module)
    return [getattr(module, x) for x in symbols if x.endswith('YaHandler')]


def extract_command(cmds, name):
    if len(cmds) == 0:
        raise Exception('Cannot load "{0}" command'.format(name))
    elif len(cmds) > 1:
        raise Exception('Cannot load "{0}" command, too many commands was imported: {1}'.format(name, cmds))
    else:
        return cmds[0]


class LazyCommand(BaseHandler):
    def __init__(self, name, loader):
        self.name = name
        self.loader = loader

    @exts.func.lazy_property
    def command(self):
        return extract_command(self.loader(), self.name)

    @exts.func.lazy_property
    def description(self):
        return self.command().description

    @exts.func.lazy_property
    def visible(self):
        return self.command().visible

    def handle(self, root_handler, args, prefix):
        return self.command().handle(root_handler, args, prefix)

    def act(self, **kwargs):
        return self.command().act(**kwargs)

    def opts_recursive(self, prefix):
        return self.command().opts_recursive(prefix)

    def dump(self):
        return self.command().dump()
