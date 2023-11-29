import re


RANGE = r'(?P<left>\(|\[)(?P<content>.*?)(?P<right>\)|\])'


class BadRangeFormat(Exception):
    pass


def is_range(ver):
    return bool(re.search(RANGE, ver))


def listify_version(ver):
    return list(map(int, re.findall(r'\d+', ver)))


def gen_filter_parsed(left, right, content):
    def raise_incorrect_format():
        raise BadRangeFormat('Incorrect range {}'.format(left + content + right))

    def less(l1, l2):
        return l1 + [0] * len(l2) < l2 + [0] * len(l1)

    x = content.split(',')

    if len(x) == 1:
        if not content.strip():
            raise_incorrect_format()

        elif left == '[' and right == ']':

            def f(v):
                return v.strip() == content.strip()

        else:
            raise_incorrect_format()

    elif len(x) == 2:
        down = listify_version(x[0].strip())
        up = listify_version(x[1].strip())

        if down:
            if left == '[':

                def lft(v):
                    return not less(listify_version(v), down)

            else:

                def lft(v):
                    return less(down, listify_version(v))

        else:
            if left == '[':
                raise_incorrect_format()

            else:

                def lft(v):
                    return True

        if up:
            if right == ']':

                def r(v):
                    return not less(up, listify_version(v))

            else:

                def r(v):
                    return less(listify_version(v), up)

        else:
            if right == ']':
                raise_incorrect_format()

            else:

                def r(v):
                    return True

        def f(v):
            return lft(v) and r(v)

    else:
        raise_incorrect_format()

    return f


def gen_filter(ver_range):
    fs = []

    re.sub(
        RANGE, lambda m: fs.append(gen_filter_parsed(m.group('left'), m.group('right'), m.group('content'))), ver_range
    )

    return lambda v: any([f(v) for f in fs])
