import os
import functools
import exts.timer


def timeit(func):
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        timer = exts.timer.Timer(__name__)
        res = func(*args, **kwargs)
        timer.show_step(func.__name__)
        return res

    return wrapper


def list_files_from(paths):
    if isinstance(paths, str):
        paths = [paths]

    for path in paths:
        if os.path.isdir(path):
            for root, _, filenames in os.walk(path):
                for filename in filenames:
                    yield os.path.join(root, filename)
        else:
            yield path
