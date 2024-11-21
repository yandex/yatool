import os
import logging
import string

import devtools.ya.test.const

import yalibrary.roman as roman
import library.python.func as func


logger = logging.getLogger(__name__)

DEFAULT_PRINT_COLOR = 'white'


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


def to_roman(x):
    if x == 0:
        return 'N'

    return roman.to_roman(x)


def percent_to_roman(d):
    d = min(int(d * 10), 999)

    p1 = d // 10
    p2 = d % 10

    return to_roman(p1) + '.' + to_roman(p2)


def max_roman_range_len(a, b):
    r = 0

    for i in range(a, b):
        r = max(r, len(to_roman(i)))

    return r


@func.lazy
def max_roman_len():
    return 1 + max_roman_range_len(1, 99) + max_roman_range_len(1, 9)


def percent_to_string(p, use_roman_numerals=False):
    if use_roman_numerals:
        r = percent_to_roman(min(p, 99.9))

        return ' ' * (max_roman_len() - len(r)) + r + '%'
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


srcs_exts = frozenset(['auxcpp', 'c', 'C', 'cc', 'cxx', 'cpp', 'cu', 'yasm', 'asm'])


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

        if devtools.ya.test.const.TestSize.is_test_shorthand(p):
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

    try:
        return join_paths(select_paths())
    except Exception as e:
        logger.exception('Failed to format paths for node (%s, %s): %s', inputs, outputs, e)
        return '<build node>'


def fmt_node(inputs, outputs, kv, tags=None, status=None):
    view = NodeView()
    view.primary.append(color_path(patch_path(format_paths(inputs, outputs, kv))))
    if status:
        view.secondary.append(status)
    if 'p' in kv:
        view.type = '[[[c:' + kv.get('pc', DEFAULT_PRINT_COLOR) + ']]' + kv['p'] + '[[rst]]]'
    view.tags = tags
    return view
