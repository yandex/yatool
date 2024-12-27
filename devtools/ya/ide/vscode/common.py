def pretty_name(name):
    return name.replace("/", "\uff0f")


def replace_prefix(s, pairs):
    for p, r in pairs:
        if s.startswith(p):
            return r + s[len(p) :]
    return s
