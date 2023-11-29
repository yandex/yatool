import yalibrary.formatter
from library.python import strings


class Truncatable:
    def __init__(self, string, min_len=4):
        self.string = string
        self.min_len = min_len

    def __str__(self):
        return self.string


def truncate(to_trim, max_len):
    markup = yalibrary.formatter.formatter.MARKUP_RE
    items = []
    size = 0
    max_len = max(max_len, to_trim.min_len)
    for item in markup.finditer(to_trim.string):
        items.append([item.end(), item.start()])
        size += item.end() - item.start()
    items_pointer = len(items) - 1
    end = len(to_trim.string)
    while end - size > max_len:
        if items and items[items_pointer][0] == end:
            end = items[items_pointer][1]
            size -= items[items_pointer][0] - items[items_pointer][1]
            items_pointer -= 1
        else:
            end -= 1
    return strings.fix_utf8(to_trim.string[:end])


def pack_status(parts, calc_len_f, max_len, trim=False):
    def conv(part):
        if isinstance(part, int):
            return [' ' * abs(part), part]
        return [part, calc_len_f(str(part))]

    parts = list(map(conv, parts))
    neg_indices = [i for i, (_, y) in enumerate(parts) if y < 0]

    min_len = sum(abs(part[1]) for part in parts)
    if trim and min_len > max_len:
        for index, (msg, msg_len) in enumerate(parts):
            if min_len <= max_len:
                break
            if isinstance(msg, Truncatable):
                parts[index][0] = truncate(msg, max_len - (min_len - msg_len))
                new_length = calc_len_f(parts[index][0])
                parts[index][1] = new_length
                min_len -= msg_len - new_length

    if len(neg_indices) and max_len > min_len:
        delta = max_len - min_len
        div, mod = delta // len(neg_indices), delta % len(neg_indices)
        for x, i in enumerate(neg_indices):
            ind = div + (x < mod) + abs(parts[i][1])
            parts[i] = [' ' * ind, ind]

    return ''.join(str(x[0]) for x in parts), sum(x[1] for x in parts)
