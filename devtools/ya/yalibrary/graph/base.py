import sys
import os
import re
import collections

from . import commands
from . import const


def hacked_normpath(path):
    if sys.platform != 'win32':
        return os.path.normpath(path)
    return _hacked_normpath_win(path)


def _hacked_normpath_win(path):
    return re.sub('/+', '/', re.sub('\\\\+', '/', path).rstrip('/'))


def loop(stack, last):
    L = []

    x = stack.pop()

    while not x == last:
        L.append(x)
        x = stack.pop()

    return [x] + L[::-1] + [last]


def traverse(items, ideps=None, before=None, after=None, on_loop=None, depth=-1):
    stack = []
    cols = collections.defaultdict(lambda: 0)

    if not ideps:
        def ideps(item):
            return item.deps

    def run(item, depth):
        if not depth:
            return
        stack.append(item)
        cols[item] = 1

        if before:
            before(item)

        for dep in ideps(item):
            if cols[dep] == 0:
                run(dep, depth - 1)

            elif cols[dep] == 1 and on_loop:
                on_loop(stack, dep)

        if after:
            after(item)

        stack.pop()
        cols[item] = 2

    for item in items:
        if cols[item] == 0:
            run(item, depth)


def parse_resources(resources_dart):
    res = {}
    for part in resources_dart:
        assert len(part) == 2
        res[part[0]] = part[1]
    return res


def uniq_first_case(lst, key=None):
    if key is None:
        def key(x):
            return x

    seen = set()

    for x in lst:
        k = key(x)

        if k not in seen:
            seen.add(k)

            yield x


def dag_transitive_closure(items, ideps=None, excludes=None, on_loop=None):
    if not ideps:
        def ideps(item):
            return item.deps

    if not excludes:
        def excludes(fr, to):
            return False

    deps = {}

    def after(item):
        s = {}
        lst = []

        def add_dep(x, dist):
            if x not in s or dist < s[x]:
                s[x] = dist
                lst.append(x)

        add_dep(item, 0)

        for d in ideps(item):
            for td, dist in deps[d]:
                add_dep(td, dist + 1)

        deps[item] = [(x, s[x]) for x in uniq_first_case(lst) if not excludes(item, x)]

    traverse(items, ideps, after=after, on_loop=on_loop)

    return deps


def fix_cmd(cmd, m):
    return commands.Cmd([m.get(arg, arg) for arg in cmd.cmd], m.get(cmd.cwd, cmd.cwd), cmd.inputs)


def iter_possible_inputs(root, srcdir, relpath):
    yield hacked_path_join(root, srcdir, relpath)
    yield hacked_path_join(root, relpath)


def _hacked_path_join_win(path, *paths):
    return hacked_normpath('/'.join([path] + list(paths)))


def hacked_path_join(path, *paths):
    if sys.platform != 'win32':
        return os.path.join(path, *paths)
    return _hacked_path_join_win(path, *paths)


def in_source(path):
    return path.startswith(const.SOURCE_ROOT[:3])


def in_build(path):
    return path.startswith(const.BUILD_ROOT[:3])
