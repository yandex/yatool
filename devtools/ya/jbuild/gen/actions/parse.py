import collections


def extract_flag(words, key):
    return key in words, [w for w in words if w != key]


def extract_word(words, key):
    for i, w in enumerate(words):
        if w == key:
            try:
                return words[i + 1], words[:i] + words[i + 2 :]

            except KeyError:
                import devtools.ya.jbuild.gen.makelist_parser2 as mp

                raise mp.ParseError('no value after {}'.format(key))

    return None, words


def extract_words(words, keys):
    kv = collections.defaultdict(list)

    k = None

    for w in words:
        if w in keys:
            k = w

        else:
            kv[k].append(w)

    return kv


def extract_chunks(words, keys):
    kv = collections.defaultdict(list)

    k = None
    cur = []

    for w in words:
        if w in keys:
            kv[k].append(cur)
            cur = []
            k = w

        else:
            cur.append(w)

    kv[k].append(cur)

    return kv
