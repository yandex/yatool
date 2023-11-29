import os
import string

import test.const


class NodeView(object):
    def __init__(self):
        self.primary = []
        self.secondary = []
        self.type = None
        self.tags = []

    def __iter__(self):
        t = '{' + ', '.join(('[[imp]]' + str(x) + '[[rst]]' for x in self.tags)) + '}' if self.tags else None
        for x in self.primary:
            for y in self.secondary + ['']:
                yield ' '.join([_f for _f in [self.type, t, x, y] if _f])


def percent_to_string(p):
    return "%4.1f%%" % min(p, 99.9)


def color_path(s):
    p = s.rfind('/')

    if p == -1:
        return s

    bef = s[: p + 1]
    aft = s[p + 1 :]

    return '[[unimp]]' + bef + '[[imp]]' + aft + '[[rst]]'


def color_path_list(path_list):
    colored_paths = []
    for path in string.split(path_list, ' '):
        colored_paths.append(color_path(path))

    return ' '.join(colored_paths)


def join_paths(inputs):
    if len(inputs) == 0:
        return 'UNKNOWN'
    if len(inputs) == 1:
        return inputs[0]

    first = inputs[0]
    last = inputs[-1]

    prefix = os.path.commonprefix([first, last])

    if len(inputs) == 2:
        delim = ', '
    else:
        delim = ' ... '

    return prefix + '{' + first[len(prefix) :] + delim + last[len(prefix) :] + '}'


def patch_path(path):
    return path.replace('$(SOURCE_ROOT)', '$(S)').replace('$(BUILD_ROOT)', '$(B)')


srcs_exts = frozenset(['c', 'C', 'cc', 'cxx', 'cpp', 'cu', 'yasm', 'asm'])


def iter_input_sources(inputs):
    for i in inputs:
        fname, ext = os.path.splitext(i)

        if ext[1:] in srcs_exts:
            yield i


def format_paths(inputs, outputs, kv):
    def select_paths():
        p = kv.get('p', '')

        if p in ('CC', 'AS'):
            if len(inputs) == 1:
                return [inputs[0]]

            srcs = list(iter_input_sources(inputs))

            if len(srcs) == 1:
                return [srcs[0]]

            if srcs:
                if len(outputs) == 1:
                    naked_output, _ = os.path.splitext(outputs[0])
                    naked_output = naked_output[10:]

                    for i in srcs:
                        if naked_output in i:
                            return [i]

                return [srcs[-1]]

            return [inputs[-1]]

        if p in ('AR', 'LD'):
            return outputs

        if test.const.TestSize.is_test_shorthand(p):
            if kv.get("path"):
                return [kv["path"]]

        if 'show_out' in kv:
            return outputs

        if len(inputs) == 1:
            if not inputs[0].endswith('.py'):
                return inputs

        if len(outputs) == 1:
            return outputs

        def cp(x):
            return len(os.path.commonprefix(x))

        return inputs if cp(inputs) > cp(outputs) else outputs

    return join_paths(select_paths())


def fmt_node(inputs, outputs, kv, tags=None, status=None):
    view = NodeView()
    view.primary.append(color_path(patch_path(format_paths(inputs, outputs, kv))))
    if status:
        view.secondary.append(status)
    if 'p' in kv and 'pc' in kv:
        view.type = '[[[c:' + kv['pc'] + ']]' + kv['p'] + '[[rst]]]'
    view.tags = tags
    return view
