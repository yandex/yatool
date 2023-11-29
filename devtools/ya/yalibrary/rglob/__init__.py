import glob
import os

from exts import os2
import exts.path2


def iter_dirs(path):
    for d, _, _ in os2.fastwalk(path):
        yield d


def rglob(pathname):
    matches = set()

    def match(path, parts):
        if len(parts) == 0:
            matches.add(path)
        else:
            head, tail = parts[0], parts[1:]
            if head == '**':
                for d in iter_dirs(path):
                    match(d, tail)
            else:
                for p in glob.iglob(os.path.join(path, head)):
                    match(p, tail)

    match(os.curdir, exts.path2.path_explode(pathname))
    return list(matches)
