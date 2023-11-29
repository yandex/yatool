from six.moves import cStringIO as StringIO

from exts import yjson

import core.yarg

from build import build_opts
from build import build_handler


class StringIOWrapper(object):
    def __init__(self, stringio):
        self.s = stringio

    def write(self, data):
        self.s.write(data)

    def getvalue(self):
        return self.s.getvalue()

    def close(self):
        self.s.close()

    def flush(self):
        self.s.flush()

    def __deepcopy__(self, memo):
        _id = id(self)
        _copy = memo.get(_id)
        if _copy is None:
            _copy = type(self)(self.s)
            memo[_id] = _copy
        return _copy


def ya_make_graph(opts, app, real_ya_make_opts=False, extra_ya_make_opts=None):
    ya_make_out = StringIOWrapper(StringIO())
    ya_make_params = core.yarg.merge_opts(build_opts.ya_make_options()).params()
    if real_ya_make_opts:
        ya_make_params = core.yarg.merge_params(ya_make_params, opts)
    if extra_ya_make_opts:
        ya_make_params = core.yarg.merge_params(ya_make_params, extra_ya_make_opts)
    ya_make_params.build_threads = 0
    ya_make_params.stdout = ya_make_out
    ya_make_params.dump_graph = 'json'
    ya_make_params.build_targets = opts.abs_targets
    ya_make_params.continue_on_fail = True
    app.execute(action=build_handler.do_ya_make)(ya_make_params)
    return yjson.loads(ya_make_out.getvalue())
