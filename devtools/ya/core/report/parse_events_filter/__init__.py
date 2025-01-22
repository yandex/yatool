def parse_events_filter(flt):
    # events can contain None, string or list(string)
    # string can contain values separated by ','
    res = set()
    if flt is not None:
        if isinstance(flt, list) or isinstance(flt, tuple):
            for v in flt:
                for ev in _split_by_column(v):
                    res.add(ev)
        elif isinstance(flt, set):
            res = flt
        else:
            for ev in _split_by_column(flt):
                res.add(ev)
    if not res:
        return None  # avoid clash with ALLOWED_FIELDS check

    return res


def _split_by_column(s):
    return [t for t in s.split(',') if t]
