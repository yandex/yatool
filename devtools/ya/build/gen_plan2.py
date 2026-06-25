from six.moves import cStringIO as StringIO

from exts import yjson

import devtools.ya.core.yarg

from devtools.ya.build import build_opts
from devtools.ya.build import build_handler


class StringIOWrapper:
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
    ya_make_params = devtools.ya.core.yarg.merge_opts(build_opts.ya_make_options()).params()
    if real_ya_make_opts:
        ya_make_params = devtools.ya.core.yarg.merge_params(ya_make_params, opts)
    if extra_ya_make_opts:
        ya_make_params = devtools.ya.core.yarg.merge_params(ya_make_params, extra_ya_make_opts)
    ya_make_params.build_threads = 0
    ya_make_params.stdout = ya_make_out
    ya_make_params.dump_graph = 'json'
    ya_make_params.build_targets = opts.abs_targets
    ya_make_params.continue_on_fail = True
    app.execute(action=build_handler.do_ya_make)(ya_make_params)
    return yjson.loads(ya_make_out.getvalue())


def ya_make_cpp_graph(opts, app):
    """Run ya make and return a ccgraph.Graph (C++ TGraph) for in-process use.

    Unlike ya_make_graph(), this does not parse the JSON back into Python dicts —
    instead it feeds the raw JSON bytes directly into the C++ graph parser via
    CppStringWrapper, avoiding a full Python-side deserialization round-trip.
    """
    import six
    from devtools.ya.build.ccgraph import CppStringWrapper, Graph

    ya_make_out = StringIOWrapper(StringIO())
    ya_make_params = devtools.ya.core.yarg.merge_opts(build_opts.ya_make_options()).params()
    ya_make_params = devtools.ya.core.yarg.merge_params(ya_make_params, opts)
    ya_make_params.build_threads = 0
    ya_make_params.stdout = ya_make_out
    ya_make_params.dump_graph = 'json'
    ya_make_params.build_targets = opts.abs_targets
    ya_make_params.continue_on_fail = True
    app.execute(action=build_handler.do_ya_make)(ya_make_params)

    wrapper = CppStringWrapper()
    wrapper.append(six.ensure_binary(ya_make_out.getvalue()))
    return Graph(ymake_output=wrapper)
